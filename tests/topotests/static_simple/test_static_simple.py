#!/usr/bin/env python
# SPDX-License-Identifier: ISC
#
# Copyright (c) 2021, LabN Consulting, L.L.C.
# Copyright (c) 2019-2020 by
# Donatas Abraitis <donatas.abraitis@gmail.com>
#
"""
Test static route functionality
"""

import datetime
import ipaddress
import math
import os
import re

import pytest
from lib.topogen import TopoRouter, Topogen
from lib.topolog import logger
from lib.common_config import retry, step

pytestmark = [pytest.mark.staticd]


@pytest.fixture(scope="module")
def tgen(request):
    "Setup/Teardown the environment and provide tgen argument to tests"

    topodef = {"s1": ("r1",), "s2": ("r1",)}

    tgen = Topogen(topodef, request.module.__name__)
    tgen.start_topology()

    router_list = tgen.routers()
    for rname, router in router_list.items():
        # Setup VRF red
        router.net.add_l3vrf("red", 10)
        router.net.add_loop("lo-red")
        router.net.attach_iface_to_l3vrf("lo-red", "red")
        router.net.attach_iface_to_l3vrf(rname + "-eth1", "red")
        #
        # router.load_frr_config("frr.conf")
        # and select daemons to run
        router.load_config(TopoRouter.RD_ZEBRA, "zebra.conf")
        router.load_config(TopoRouter.RD_MGMTD)
        router.load_config(TopoRouter.RD_STATIC)

    tgen.start_router()
    yield tgen
    tgen.stop_topology()


def get_ip_networks(super_prefix, count):
    count_log2 = math.log(count, 2)
    if count_log2 != int(count_log2):
        count_log2 = int(count_log2) + 1
    else:
        count_log2 = int(count_log2)
    network = ipaddress.ip_network(super_prefix)
    return tuple(network.subnets(count_log2))[0:count]


def get_src_networks(src_prefix, count, default=""):
    if src_prefix is not None:
        for net in get_ip_networks(src_prefix, count):
            yield " from {}".format(net)
    else:
        for i in range(0, count):
            yield default


def enable_debug(router):
    router.vtysh_cmd("debug northbound callbacks configuration")


def disable_debug(router):
    router.vtysh_cmd("no debug northbound callbacks configuration")


@retry(retry_timeout=30, initial_wait=0.1)
def check_kernel(r1, super_prefix, src_prefix, count, add, is_blackhole, vrf, matchvia):
    network = ipaddress.ip_network(super_prefix)
    vrfstr = f" vrf {vrf}" if vrf else ""
    if network.version == 6:
        kernel = r1.run(f"ip -6 route show{vrfstr}")
    else:
        kernel = r1.run(f"ip -4 route show{vrfstr}")

    logger.debug("checking kernel routing table%s:\n%s", vrfstr, kernel)
    for net, srcnet in zip(
        get_ip_networks(super_prefix, count), get_src_networks(src_prefix, count)
    ):
        netfull = str(net) + srcnet
        if not add:
            assert netfull + " nhid" not in kernel
            assert netfull + " via" not in kernel
            continue

        if is_blackhole:
            route = f"blackhole {netfull}(?: dev lo)? proto (static|196) metric 20"
        else:
            route = (
                f"{netfull}(?: nhid [0-9]+)? {matchvia} proto (static|196) metric 20"
            )
        assert re.search(route, kernel), f"Failed to find \n'{route}'\n in \n'{kernel}'"


def do_config_inner(
    r1,
    count,
    add=True,
    do_ipv6=False,
    do_ipv6_nexthop=False,
    do_sadr=False,
    via=None,
    vrf=None,
    use_cli=False,
):
    optype = "adding" if add else "removing"
    iptype = "IPv6" if do_ipv6 else "IPv4"

    #
    # Set the route details
    #
    src_prefs = [None, None]
    if do_ipv6 and do_sadr:
        # intentionally using overlapping prefix
        super_prefs = ["2001::/48", "2002::/48"]
        src_prefs = ["2001:db8:1111::/48", "2001:db8:2222::/48"]
    elif do_ipv6:
        super_prefs = ["2001::/48", "2002::/48"]
    elif do_ipv6_nexthop:
        super_prefs = ["11.0.0.0/8", "21.0.0.0/8"]
    else:
        super_prefs = ["10.0.0.0/8", "20.0.0.0/8"]

    super_prefix = super_prefs[1 if vrf else 0]
    src_prefix = src_prefs[1 if vrf else 0]

    matchvia = ""
    if via == "blackhole":
        pass
    elif via:
        matchvia = f"dev {via}"
    else:
        if vrf:
            via = "2102::2" if do_ipv6 or do_ipv6_nexthop else "102.0.0.2"
            matchvia = (
                f"via inet6 {via} dev r1-eth1"
                if not do_ipv6 and do_ipv6_nexthop
                else f"via {via} dev r1-eth1"
            )
        else:
            via = "2101::2" if do_ipv6 or do_ipv6_nexthop else "101.0.0.2"
            matchvia = (
                f"via inet6 {via} dev r1-eth0"
                if not do_ipv6 and do_ipv6_nexthop
                else f"via {via} dev r1-eth0"
            )

    vrfdbg = " in vrf {}".format(vrf) if vrf else ""
    logger.debug("{} {} static {} routes{}".format(optype, count, iptype, vrfdbg))

    #
    # Generate config file in a retrievable place
    #

    config_file = os.path.join(
        r1.logdir, r1.name, "{}-routes-{}.conf".format(iptype.lower(), optype)
    )
    with open(config_file, "w") as f:
        if use_cli:
            f.write("configure terminal\n")
        if vrf:
            f.write("vrf {}\n".format(vrf))

        for net, srcnet in zip(
            get_ip_networks(super_prefix, count), get_src_networks(src_prefix, count)
        ):
            if add:
                f.write("ip route {}{} {}\n".format(net, srcnet, via))
            else:
                f.write("no ip route {}{} {}\n".format(net, srcnet, via))

    #
    # Load config file.
    #

    if use_cli:
        load_command = 'vtysh < "{}"'.format(config_file)
    else:
        load_command = 'vtysh -f "{}"'.format(config_file)
    tstamp = datetime.datetime.now()
    output = r1.cmd_raises(load_command)
    delta = (datetime.datetime.now() - tstamp).total_seconds()

    #
    # Verify the results are in the kernel
    #
    check_kernel(
        r1, super_prefix, src_prefix, count, add, via == "blackhole", vrf, matchvia
    )

    optyped = "added" if add else "removed"
    logger.debug(
        "{} {} {} static routes under {}{} in {}s".format(
            optyped, count, iptype.lower(), super_prefix, vrfdbg, delta
        )
    )


def do_config(*args, **kwargs):
    do_config_inner(*args, do_ipv6=False, do_sadr=False, **kwargs)
    do_config_inner(*args, do_ipv6=False, do_ipv6_nexthop=True, **kwargs)
    do_config_inner(*args, do_ipv6=True, do_sadr=False, **kwargs)
    do_config_inner(*args, do_ipv6=True, do_sadr=True, **kwargs)


def guts(tgen, vrf, use_cli):
    if tgen.routers_have_failure():
        pytest.skip(tgen.errors)

    r1 = tgen.routers()["r1"]

    count = 10
    step(f"add {count} via gateway", reset=True)
    do_config(r1, count, True, vrf=vrf, use_cli=use_cli)
    step(f"remove {count} via gateway")
    do_config(r1, count, False, vrf=vrf, use_cli=use_cli)

    via = f"lo-{vrf}" if vrf else "lo"
    step("add via loopback")
    do_config(r1, 1, True, via=via, vrf=vrf, use_cli=use_cli)
    step("remove via loopback")
    do_config(r1, 1, False, via=via, vrf=vrf, use_cli=use_cli)

    step("add via blackhole")
    do_config(r1, 1, True, via="blackhole", vrf=vrf, use_cli=use_cli)
    step("remove via blackhole")
    do_config(r1, 1, False, via="blackhole", vrf=vrf, use_cli=use_cli)


def test_static_cli(tgen):
    guts(tgen, "", True)


def test_static_file(tgen):
    guts(tgen, "", False)


def test_static_vrf_cli(tgen):
    guts(tgen, "red", True)


def test_static_vrf_file(tgen):
    guts(tgen, "red", False)
