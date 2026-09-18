#!/bin/bash -eu
# Runs a test command in an isolated environment using bubblewrap (bwrap).
# This provides PID and network namespace isolation so tests can run in
# parallel without interfering with each other.
#
# Network isolation with slave bridging:
#   Each sandbox gets a private virtual L2 segment to its allocated slave(s)
#   via VXLAN tunnel(s). This ensures:
#   - The fixed master IP (TEST_IP_MASTER) is reused identically in every
#     sandbox without conflicts (each namespace is fully isolated).
#   - Multicast between sandbox and slaves works normally on the private segment.
#   - Multicast does NOT leak to the host, other sandboxes, or other slaves.
#
#   Both ends get a per-test network namespace, so no network state (conntrack,
#   neighbours, routes, sockets) survives from one test to the next.
#
#   Architecture (eth0 is never modified):
#     [master namespace]
#       eth0 ─── native underlay connectivity to slaves (Docker macvlan)
#       vx_${VNI}_$$ ─── VXLAN tunnel per slave (uses eth0 for transport)
#       br_$$ ─── bridge connecting all VXLANs and sandbox veth
#       veth_host_$$ ─── bridge port, paired with sandbox veth
#     [sandbox netns]
#       veth_$$ (FIXED_MASTER_IP) ═══ private L2 via bridge+vxlan ═══ slave(s)
#     [slave container, default netns]
#       eth0 ─── Docker underlay, untouched; carries the VXLAN transport
#       overlay0 (VXLAN, SANDBOX_NET.10x) ─── control plane: sshd answers here
#     [slave netns isol_$$]
#       eth0 (macvlan on overlay0, TEST_IP_SLAVE) ─── data plane, per test
#
#   Control and data are split because sshd cannot follow the test into a new
#   namespace: the tunnel endpoint stays where sshd is, the test's does not.
#
#   With 0 slaves, only PID and network namespace isolation is created
#   (no VXLAN, no bridge — just loopback in the sandbox).
#
# Environment variables:
#   SANDBOX_MASTER_IP         Fixed master IP inside sandbox. Default 10.99.0.254.
#   ISOLATED_SUBNET           Subnet prefix length. Defaults to 24.
#   ISOLATED_NET_IF           Host network interface for VXLAN underlay.
#                             Defaults to eth0.
#   CTEST_RESOURCE_GROUP_COUNT
#                             Set by CTest: number of allocated resource groups
#                             (= number of slaves). 0 or unset means no slaves.
#   CTEST_RESOURCE_GROUP_<N>_SLAVES
#                             Set by CTest: allocated slave resource per group
#                             (format: "id:<ip_underscored>,slots:1").
#   SSH_KEY                   Path to SSH private key for slave access.
#                             Defaults to
#                             /commonapi_main/lxc-config/.ssh/mgc_lxc/rsa_key_file.pub
#   PREFLIGHT_PORT            Closed UDP port on the slave used by the preflight.
#                             Defaults to 34599.
#   PREFLIGHT_DEADLINE        Seconds to wait for a usable UDP path before
#                             failing the test as infrastructure. Defaults to 15.

COMMAND="${*}"
echo "Executing in an isolated environment: ${COMMAND}"

# --- Configuration -----------------------------------------------------------

# Sandbox overlay IPs — must be on a different subnet than the Docker macvlan
# network so that VXLAN route setup cannot break underlay connectivity.
# Master: *.254, Slaves: *.1, *.2, *.3, ...
SANDBOX_NET="${SANDBOX_NET:-10.99.0}"
FIXED_MASTER_IP="${SANDBOX_MASTER_IP:-${SANDBOX_NET}.254}"
SUBNET_MASK="${ISOLATED_SUBNET:-24}"
NET_IF="${ISOLATED_NET_IF:-eth0}"
SSH_KEY="${SSH_KEY:-/commonapi_main/lxc-config/.ssh/mgc_lxc/rsa_key_file.pub}"
SSH_OPTS=(-o StrictHostKeyChecking=no -o BatchMode=yes -i "${SSH_KEY}"
          -o ServerAliveInterval=5 -o ServerAliveCountMax=3)

# Where this script (and its helpers) live.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Preflight probe port: nothing binds it, being above the highest fixed test
# port (34516) and below the netns ephemeral range (34600+).
PREFLIGHT_PORT="${PREFLIGHT_PORT:-34599}"
PREFLIGHT_DEADLINE="${PREFLIGHT_DEADLINE:-15}"

# Shared directory paths (tmpfs-backed volume shared between master and slave containers).
SHARED_BASE="/home/test-shared"
SHARED_DIR="${SHARED_BASE}/isol_$$"
SHARED_FIXED_PATH="${TEST_SHARED_DIR:-/home/shared}"

# --- Determine allocated slaves -----------------------------------------------

# Number of slaves allocated by CTest (0 = no slaves needed).
NUM_SLAVES="${CTEST_RESOURCE_GROUP_COUNT:-0}"

# Extract actual Docker IPs of allocated slaves from CTest resource variables.
declare -a SLAVE_DOCKER_IPS=()
for ((i = 0; i < NUM_SLAVES; i++)); do
    varname="CTEST_RESOURCE_GROUP_${i}_SLAVES"
    raw="${!varname:-}"
    if [[ -z "$raw" ]]; then
        echo "INFRASTRUCTURE ERROR: ${varname} not set but CTEST_RESOURCE_GROUP_COUNT=${NUM_SLAVES}" >&2
        exit 1
    fi
    ip=$(printf '%s' "$raw" | sed 's/id:\([^,]*\).*/\1/' | tr '_' '.')
    SLAVE_DOCKER_IPS+=("$ip")
done

# Master's own IP on the Docker network (for VXLAN local endpoint).
MASTER_ETH0_IP=$(ip -4 addr show "$NET_IF" | grep -oP '(?<=inet )\S+' | cut -d/ -f1 | head -1)

# --- Environment variable overrides ------------------------------------------

# Override IPs to sandbox values so tests communicate via the VXLAN overlay.
# bwrap inherits the environment, so exporting here is sufficient.
export TEST_IP_MASTER="${FIXED_MASTER_IP}"
if ((NUM_SLAVES >= 1)); then
    export TEST_IP_SLAVE="${SANDBOX_NET}.1"
fi
if ((NUM_SLAVES >= 2)); then
    export TEST_IP_SLAVE_SECOND="${SANDBOX_NET}.2"
fi

# --- Network namespace and interface setup ------------------------------------

NS_NAME="isol_$$"
BRIDGE_IF="br_$$"
VETH_HOST="vh_$$"
VETH_SANDBOX="vs_$$"
SLAVE_NS="$NS_NAME"

cleanup() {
    if ((NUM_SLAVES > 0)); then
        # Drop the shared directory symlink and the per-test namespace.
        for ((i = 0; i < NUM_SLAVES; i++)); do
            slave_vni="${SLAVE_DOCKER_IPS[$i]##*.}"
            timeout 10 ssh "${SSH_OPTS[@]}" -o ConnectTimeout=5 root@"${SLAVE_DOCKER_IPS[$i]}" "
                rm -f ${SHARED_FIXED_PATH}
                # ip netns del drops the name, not the inhabitants.
                ip netns pids ${SLAVE_NS} 2>/dev/null | xargs -r kill -9 2>/dev/null || true
                ip netns del ${SLAVE_NS} 2>/dev/null || true
                # mv_ covers a run killed before the macvlan was moved in.
                ip link del overlay0 2>/dev/null || true
                ip link del mv_${slave_vni} 2>/dev/null || true
            " 2>/dev/null || true
        done

        # Remove bridge (detaches all ports and removes the bridge device).
        ip link del "$BRIDGE_IF" 2>/dev/null || true

        # Remove VXLAN devices from master namespace.
        for ((i = 0; i < NUM_SLAVES; i++)); do
            local_vni="${SLAVE_DOCKER_IPS[$i]##*.}"
            ip link del "vx_${local_vni}_$$" 2>/dev/null || true
        done

        # Remove the per-test shared directory.
        rm -rf "${SHARED_DIR}"
    fi

    # Remove network namespace (also removes veth_sandbox, which destroys the pair).
    ip netns del "$NS_NAME" 2>/dev/null || true
    return 0
}
trap cleanup EXIT

# --- Pre-cleanup: remove stale resources from previous aborted runs -----------

# Remove any leftover netns with the same name (possible PID recycling).
if ip netns list 2>/dev/null | grep -qw "$NS_NAME"; then
    ip netns del "$NS_NAME" 2>/dev/null || true
    # Wait for the kernel to finish unmounting the stale bind mount.
    while [[ -e "/run/netns/$NS_NAME" ]]; do sleep 0.01; done
fi

# Namespaces left by a run that died before its EXIT trap. Tests run in parallel
# here, so only reap the ones whose owning process is gone.
for stale_ns in $(ip netns list 2>/dev/null | grep -oE '^isol_[0-9]+' || true); do
    [[ -d "/proc/${stale_ns#isol_}" ]] || ip netns del "$stale_ns" 2>/dev/null || true
done

# Remove stale bridge and veth (from a killed previous run with same PID).
ip link del "$BRIDGE_IF" 2>/dev/null || true
ip link del "$VETH_HOST" 2>/dev/null || true

if ((NUM_SLAVES > 0)); then
    # Remove any stale master-side VXLAN devices for the same VNI(s).
    # Since a slave is exclusively allocated, any existing VXLAN with its VNI
    # on this host is guaranteed stale (from a killed previous run).
    for ((i = 0; i < NUM_SLAVES; i++)); do
        stale_vni="${SLAVE_DOCKER_IPS[$i]##*.}"
        for stale_dev in $(ip -o link show 2>/dev/null | grep -oP "vx_${stale_vni}_\d+" || true); do
            ip link del "$stale_dev" 2>/dev/null || true
        done
    done

    # Leftovers from a killed run with the same PID. The underlay0 branch
    # repairs a slave renamed by the previous version of this script.
    rm -rf "${SHARED_DIR}"
    for ((i = 0; i < NUM_SLAVES; i++)); do
        stale_vni="${SLAVE_DOCKER_IPS[$i]##*.}"
        timeout 5 ssh "${SSH_OPTS[@]}" -o ConnectTimeout=3 root@"${SLAVE_DOCKER_IPS[$i]}" "
            rm -f ${SHARED_FIXED_PATH}
            for stale_ns in \$(ip netns list 2>/dev/null | grep -oE '^isol_[0-9]+' || true); do
                ip netns pids \"\$stale_ns\" 2>/dev/null | xargs -r kill -9 2>/dev/null || true
                ip netns del \"\$stale_ns\" 2>/dev/null || true
            done
            ip link del overlay0 2>/dev/null || true
            ip link del mv_${stale_vni} 2>/dev/null || true
            if ip link show underlay0 &>/dev/null; then
                ip link del eth0 2>/dev/null || true
                ip link set underlay0 down && ip link set underlay0 name eth0 && ip link set eth0 up
            fi
        " 2>/dev/null || true
    done
fi

# Create an isolated network namespace (retry briefly if a concurrent cleanup
# left a stale mount that the kernel hasn't fully released yet).
for attempt in 1 2 3 4 5; do
    if ip netns add "$NS_NAME" 2>/tmp/netns_add_err_$$; then
        break
    fi
    echo "WARNING: ip netns add $NS_NAME failed (attempt $attempt/5):" >&2
    cat /tmp/netns_add_err_$$ >&2
    echo "  /run/netns/$NS_NAME exists: $([[ -e "/run/netns/$NS_NAME" ]] && echo yes || echo no)" >&2
    echo "  /run/netns/ contents: $(ls -la /run/netns/ 2>&1)" >&2
    echo "  ip netns list: $(ip netns list 2>&1)" >&2
    echo "  mount | grep netns: $(mount 2>/dev/null | grep netns)" >&2
    # Clean up stale bind mount if present.
    ip netns del "$NS_NAME" 2>/dev/null || true
    while [[ -e "/run/netns/$NS_NAME" ]]; do sleep 0.01; done
    if ((attempt == 5)); then
        echo "INFRASTRUCTURE ERROR: Failed to create network namespace $NS_NAME after 5 retries" >&2
        echo "  PID=$$, uid=$(id), capabilities=$(cat /proc/self/status | grep -i cap)" >&2
        rm -f /tmp/netns_add_err_$$
        exit 1
    fi
done
rm -f /tmp/netns_add_err_$$

# The sysctls docker-compose.yaml sets on the container are per-netns and are NOT
# inherited by a new one: a per-test netns starts from the kernel defaults, which
# hand out the fixed SOME/IP test ports as ephemeral source ports. So re-apply
# them there, reading the values from the container so both sides stay identical.
# Docker mounts /proc/sys read-only, hence the remount.
LOCAL_PORT_RANGE="$(sysctl -n net.ipv4.ip_local_port_range | awk '{print $1, $2}')"
TCP_TW_REUSE="$(sysctl -n net.ipv4.tcp_tw_reuse)"

# Printed as a script so the master (locally) and the slave (over ssh) run exactly
# the same code. The read-back is not redundant: sysctl -wq exits 0 even when the
# write is refused, so a silently unapplied value is only caught by reading it.
netns_sysctls() {
    cat <<EOF
mount -o remount,rw /proc/sys 2>/dev/null || true
sysctl -wq "net.ipv4.ip_local_port_range=${LOCAL_PORT_RANGE}"
sysctl -wq "net.ipv4.tcp_tw_reuse=${TCP_TW_REUSE}"
sysctl -n net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse |
    awk -v range="${LOCAL_PORT_RANGE}" -v reuse="${TCP_TW_REUSE}" '
        NR == 1 { if (\$1 " " \$2 != range) exit 1 }
        NR == 2 { if (\$1 != reuse) exit 1 }
        END { if (NR != 2) exit 1 }'
EOF
}

if netns_sysctls | ip netns exec "$NS_NAME" sh -e; then
    echo "Applied container sysctls in netns $NS_NAME:" \
         "ip_local_port_range=${LOCAL_PORT_RANGE} tcp_tw_reuse=${TCP_TW_REUSE}" >&2
else
    echo "INFRASTRUCTURE ERROR: Could not apply the container sysctls in netns $NS_NAME" >&2
    exit 1
fi

if ((NUM_SLAVES == 0)); then
    # --- No slaves: just isolated namespace with loopback ---------------------
    ip netns exec "$NS_NAME" ip link set lo up

else
    # --- Configure slaves and VXLAN tunnels -----------------------------------

    # Shipped to the slave as base64 so no quoting survives the ssh round trip.
    NETNS_SYSCTLS_B64="$(netns_sysctls | base64 -w0)"

    # Management address of each slave, in allocation order.
    declare -a SLAVE_MGMT_IPS=()

    # Bridge connecting all VXLANs to sandbox veth.
    ip link add "$BRIDGE_IF" type bridge stp_state 0 forward_delay 0
    ip link set "$BRIDGE_IF" up

    for ((i = 0; i < NUM_SLAVES; i++)); do
        slave_docker_ip="${SLAVE_DOCKER_IPS[$i]}"
        slave_sandbox_ip="${SANDBOX_NET}.$((i + 1))"
        vni="${slave_docker_ip##*.}"
        vxlan_port=$((10000 + vni))
        vxlan_if="vx_${vni}_$$"
        # Clear of the test addresses (master .254, slaves .1, .2, ...).
        slave_mgmt_ip="${SANDBOX_NET}.$((101 + i))"
        SLAVE_MGMT_IPS+=("$slave_mgmt_ip")

        # Verify that the slave is reachable.
        if ! ping -c 1 -W 3 "$slave_docker_ip" >/dev/null 2>&1; then
            echo "INFRASTRUCTURE ERROR: Slave $((i + 1)) at ${slave_docker_ip} is not reachable." >&2
            exit 1
        fi

        # Two endpoints on one tunnel, because sshd runs in the container's
        # default namespace and cannot follow the test into another one:
        #   overlay0  the VXLAN itself, left here with a management address, so
        #             exec_slave keeps reaching sshd;
        #   eth0      a macvlan on overlay0, moved into the per-test namespace,
        #             holding TEST_IP_SLAVE. Test sockets therefore live in a
        #             namespace whose conntrack, neighbours and rules start empty,
        #             and slave-side scripts still address a device named eth0.
        # Docker's eth0 is never renamed or moved, so a run killed midway cannot
        # leave the container without networking.
        timeout 60 ssh "${SSH_OPTS[@]}" -o ConnectTimeout=10 root@"${slave_docker_ip}" "
            set -e
            ip netns del ${SLAVE_NS} 2>/dev/null || true
            # ip netns del detaches the mount asynchronously; re-adding the same
            # name before the kernel is done races it, as the master side found.
            # Bounded: an unbounded spin outlives the ssh that timeout 60 kills.
            for _ in \$(seq 200); do [ -e /run/netns/${SLAVE_NS} ] || break; sleep 0.01; done
            ip link del overlay0 2>/dev/null || true
            ip link del mv_${vni} 2>/dev/null || true
            ip netns add ${SLAVE_NS}
            echo '${NETNS_SYSCTLS_B64}' | base64 -d | ip netns exec ${SLAVE_NS} sh -e
            ip link add overlay0 type vxlan id ${vni} \
                remote ${MASTER_ETH0_IP} local ${slave_docker_ip} dstport ${vxlan_port} dev eth0
            ip link set overlay0 up
            ip addr add ${slave_mgmt_ip}/${SUBNET_MASK} dev overlay0 noprefixroute
            ip route replace ${FIXED_MASTER_IP}/32 dev overlay0
            ip link add link overlay0 name mv_${vni} type macvlan mode bridge
            ip link set mv_${vni} netns ${SLAVE_NS}
            ip netns exec ${SLAVE_NS} sh -ec \"
                ip link set lo up
                ip link set mv_${vni} name eth0
                ip link set eth0 up
                ip addr add ${slave_sandbox_ip}/${SUBNET_MASK} dev eth0 noprefixroute
                ip route replace ${FIXED_MASTER_IP}/32 dev eth0
                ip route replace ${SANDBOX_NET}.0/${SUBNET_MASK} dev eth0
                ip route replace multicast 224.0.0.0/4 dev eth0
            \"
        " || {
            ssh_rc=$?
            if ((ssh_rc == 124)); then
                echo "INFRASTRUCTURE ERROR: Timed out configuring slave $((i + 1)) at ${slave_docker_ip}" >&2
            else
                echo "INFRASTRUCTURE ERROR: Failed to configure the namespace/tunnel on slave $((i + 1)) at ${slave_docker_ip}" >&2
            fi
            exit 1
        }

        # VXLAN tunnel endpoint in master namespace, bound to eth0.
        ip link add "$vxlan_if" type vxlan \
            id "$vni" remote "$slave_docker_ip" local "$MASTER_ETH0_IP" \
            dstport "$vxlan_port" dev "$NET_IF"
        ip link set "$vxlan_if" up
        ip link set "$vxlan_if" master "$BRIDGE_IF"
    done

    # Veth pair connecting sandbox to bridge.
    ip link add "$VETH_HOST" type veth peer name "$VETH_SANDBOX"
    ip link set "$VETH_HOST" master "$BRIDGE_IF"
    ip link set "$VETH_HOST" up

    # Sandbox end moves to the isolated namespace.
    ip link set "$VETH_SANDBOX" netns "$NS_NAME"

    # Configure the sandbox's network interface.
    # Rename the veth to eth0 so tests can use a well-known interface name.
    ip netns exec "$NS_NAME" sh -ec "
        ip link set lo up
        ip link set ${VETH_SANDBOX} name eth0
        ip link set eth0 up
        ip addr add ${FIXED_MASTER_IP}/${SUBNET_MASK} dev eth0 noprefixroute
        ip route add ${SANDBOX_NET}.0/${SUBNET_MASK} dev eth0
        ip route add multicast 224.0.0.0/4 dev eth0
    "

    # Create per-test shared directory and symlink it on each slave.
    mkdir -p "${SHARED_DIR}"
    for ((i = 0; i < NUM_SLAVES; i++)); do
        timeout 60 ssh "${SSH_OPTS[@]}" -o ConnectTimeout=10 root@"${SLAVE_DOCKER_IPS[$i]}" \
            "ln -sfn ${SHARED_DIR} ${SHARED_FIXED_PATH}" || {
            ssh_rc=$?
            if ((ssh_rc == 124)); then
                echo "INFRASTRUCTURE ERROR: Timed out linking the shared directory on slave $((i + 1))" >&2
            else
                echo "INFRASTRUCTURE ERROR: Could not link the shared directory on slave $((i + 1))" >&2
            fi
            exit 1
        }
    done

    # A path can carry pings while UDP is refused, so wait for UDP itself.
    # Failing here says "infrastructure" instead of surfacing inside the test.
    PREFLIGHT="${SCRIPT_DIR}/udp_preflight.py"
    for ((i = 0; i < NUM_SLAVES; i++)); do
        slave_sandbox_ip="${SANDBOX_NET}.$((i + 1))"
        if [[ ! -r "$PREFLIGHT" ]] || ! command -v python3 >/dev/null 2>&1; then
            # Tooling missing, not a network problem: say so and fall back to ICMP.
            echo "WARNING: udp_preflight.py or python3 unavailable, falling back to ping" >&2
            if ! ip netns exec "$NS_NAME" ping -c 1 -W 3 "$slave_sandbox_ip" >/dev/null 2>&1; then
                echo "WARNING: Cannot reach slave $((i + 1)) at ${slave_sandbox_ip} from sandbox" >&2
            fi
        elif ! ip netns exec "$NS_NAME" python3 "$PREFLIGHT" \
                --port "$PREFLIGHT_PORT" --deadline "$PREFLIGHT_DEADLINE" "$slave_sandbox_ip"; then
            echo "INFRASTRUCTURE ERROR: No usable UDP path to slave $((i + 1)) at ${slave_sandbox_ip};" >&2
            echo "                      the tunnel is up but UDP does not get through." >&2
            exit 1
        fi
    done
fi

# Export the fixed shared dir path so tests can use it.
export TEST_SHARED_DIR="${SHARED_FIXED_PATH}" # re-export for bwrap child

# setup_test.sh needs both: where to ssh, and which namespace to run in.
if ((NUM_SLAVES > 0)); then
    export TEST_SLAVE_NETNS="${SLAVE_NS}"
    export TEST_SLAVE_SSH_IP="${SLAVE_MGMT_IPS[0]}"
fi

# --- Execute test inside the sandbox ------------------------------------------

# Capture network traffic inside the sandbox (if PCAP_DIR is set).
TCPDUMP_PID=""
if [[ "${PCAP_DIR:-}" && "${CTEST_TEST_NAME:-}" ]]; then
    ip netns exec "$NS_NAME" \
        tcpdump -U -i any -w "${PCAP_DIR}/${CTEST_TEST_NAME}.pcap" 2>/dev/null &
    TCPDUMP_PID=$!
fi

# Run bwrap inside the pre-configured network namespace. Only PID isolation is
# added by bwrap (network is already isolated via the namespace).
# Build bwrap arguments. When slaves are present, bind-mount the per-test
# shared directory to the fixed path inside the sandbox.
BWRAP_EXTRA=()
if ((NUM_SLAVES > 0)); then
    BWRAP_EXTRA+=(--bind "${SHARED_DIR}" "${SHARED_FIXED_PATH}")
fi

TEST_RC=0

# Need to hide /run/netns before invocation of bwrap, otherwise it gets confused
# if another test deletes its namespace between enumeration and recursive bind mount.
ip netns exec "$NS_NAME" \
    unshare -m -- sh -c '
        mount -t tmpfs tmpfs /run/netns
        exec "$@"
    ' _ \
    bwrap \
    --unshare-pid \
    --die-with-parent \
    --bind / / \
    --dev /dev \
    --proc /proc \
    --perms 1777 --tmpfs /tmp \
    "${BWRAP_EXTRA[@]}" \
    -- \
    sh -c "${COMMAND}; retval=\$?; kill -s 9 -1 2>/dev/null; exit \$retval" ||
    TEST_RC=$?

# Stop tcpdump.
if [[ "$TCPDUMP_PID" ]]; then
    kill "$TCPDUMP_PID" 2>/dev/null || true
    wait "$TCPDUMP_PID" 2>/dev/null || true
fi

exit "$TEST_RC"
