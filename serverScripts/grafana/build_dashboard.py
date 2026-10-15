#!/usr/bin/python3 -I
"""Builds forever-server.json, the Grafana dashboard for the Forever Linux server (Telegraf job 'telegraf-forever' in Prometheus).

    python3 -I build_dashboard.py > forever-server.json
Upload with the Grafana HTTP API (POST /api/dashboards/db with {"dashboard": <this file>, "overwrite": true}) or import it in the UI.
The bot panels need the exec input from ../telegraf/install-telegraf-bots.sh.
"""
import json

DS = {"type": "prometheus", "uid": "cfuvt9u3mp3i8e"}
JOB = 'job="telegraf-forever"'
_id = [0]


def nid():
    _id[0] += 1
    return _id[0]


def target(expr, legend="", ref="A"):
    return {"datasource": DS, "expr": expr, "legendFormat": legend, "refId": ref, "editorMode": "code", "range": True}


def stat(title, expr, x, y, w=4, unit="none", thresholds=None, mappings=None, decimals=None, desc=""):
    steps = thresholds or [("green", None)]
    field = {"unit": unit, "mappings": mappings or [],
             "thresholds": {"mode": "absolute", "steps": [{"color": c, "value": v} for c, v in steps]}}
    if decimals is not None:
        field["decimals"] = decimals
    return {"id": nid(), "type": "stat", "title": title, "description": desc, "datasource": DS, "gridPos": {"x": x, "y": y, "w": w, "h": 4},
            "targets": [target(expr)], "fieldConfig": {"defaults": field, "overrides": []},
            "options": {"reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False}, "colorMode": "background", "graphMode": "none",
                        "textMode": "auto", "justifyMode": "center"}}


def series(title, targets, x, y, w=12, h=8, unit="none", stack=False, minimum=None, maximum=None, desc=""):
    custom = {"drawStyle": "line", "lineWidth": 1, "fillOpacity": 25 if stack else 10, "showPoints": "never", "spanNulls": True,
              "stacking": {"mode": "normal" if stack else "none", "group": "A"}}
    field = {"unit": unit, "custom": custom}
    if minimum is not None:
        field["min"] = minimum
    if maximum is not None:
        field["max"] = maximum
    return {"id": nid(), "type": "timeseries", "title": title, "description": desc, "datasource": DS, "gridPos": {"x": x, "y": y, "w": w, "h": h},
            "targets": [dict(t, refId=chr(65 + i)) for i, t in enumerate(targets)],
            "fieldConfig": {"defaults": field, "overrides": []},
            "options": {"legend": {"displayMode": "list", "placement": "bottom", "showLegend": True}, "tooltip": {"mode": "multi", "sort": "desc"}}}


def row(title, y):
    return {"id": nid(), "type": "row", "title": title, "collapsed": False, "gridPos": {"x": 0, "y": y, "w": 24, "h": 1}, "panels": []}


SERVICE_MAP = [{"type": "value", "options": {
    "0": {"text": "running", "color": "green", "index": 0}, "2": {"text": "stopped", "color": "orange", "index": 1},
    "3": {"text": "FAILED", "color": "red", "index": 2}, "1": {"text": "reloading", "color": "yellow", "index": 3}}}]


def service(name, label, x):
    return stat(label, f'systemd_units_active_code{{{JOB},name="{name}"}}', x, 1, w=4, mappings=SERVICE_MAP,
                thresholds=[("green", None)], desc="systemd state of " + name)


panels = []
panels.append(row("Servers and machine", 0))
panels += [
    service("forever-prod.service", "Prod worldserver", 0),
    service("forever-sim.service", "Sim worldserver", 4),
    service("forever-prod-bnet.service", "Login server", 8),
    service("forever-dashboard.service", "Dashboard", 12),
    stat("CPU used", f'100 - cpu_usage_idle{{{JOB},cpu="cpu-total"}}', 16, 1, unit="percent", decimals=0,
         thresholds=[("green", None), ("orange", 70), ("red", 90)]),
    stat("Memory used", f'mem_used_percent{{{JOB}}}', 20, 1, unit="percent", decimals=0,
         thresholds=[("green", None), ("orange", 75), ("red", 90)]),
]
panels.append(row("Bots on the sim (needs the bots exec input)", 5))
panels += [
    stat("Bots online", f'forever_bots_online{{{JOB},server="sim"}}', 0, 6, decimals=0),
    stat("In combat", f'forever_bots_in_combat{{{JOB},server="sim"}}', 4, 6, decimals=0),
    stat("Still level 1", f'forever_bots_level1{{{JOB},server="sim"}}', 8, 6, decimals=0, thresholds=[("green", None), ("orange", 50), ("red", 150)]),
    stat("Average level", f'forever_bots_avg_level{{{JOB},server="sim"}}', 12, 6, decimals=1),
    stat("World updates / s", f'rate(forever_bots_tick{{{JOB},server="sim"}}[1m])', 16, 6, decimals=0, unit="none",
         desc="How many times per second the sim's world updates. Under 20 a bot-heavy world starts to feel slow."),
    stat("Time per world update", f'1000 / rate(forever_bots_tick{{{JOB},server="sim"}}[1m])', 20, 6, unit="ms", decimals=0,
         thresholds=[("green", None), ("orange", 40), ("red", 100)]),
]
panels += [
    series("Bots", [target(f'forever_bots_online{{{JOB},server="sim"}}', "online"), target(f'forever_bots_in_combat{{{JOB},server="sim"}}', "in combat"),
                    target(f'forever_bots_level1{{{JOB},server="sim"}}', "level 1"), target(f'forever_bots_dead{{{JOB},server="sim"}}', "dead")], 0, 10),
    series("World update time vs bots", [target(f'1000 / rate(forever_bots_tick{{{JOB},server="sim"}}[1m])', "ms per update"),
                                         target(f'forever_bots_online{{{JOB},server="sim"}}', "bots online")], 12, 10,
           desc="The slowdown analysis: how the time per world update grows with the bot count."),
    series("Bot levels", [target(f'forever_bots_avg_level{{{JOB},server="sim"}}', "average"), target(f'forever_bots_max_level{{{JOB},server="sim"}}', "highest")], 0, 18),
    series("Creatures the bots see / maps in use", [target(f'forever_bots_units{{{JOB},server="sim"}}', "creatures listed"),
                                                    target(f'forever_bots_maps{{{JOB},server="sim"}}', "maps with bots")], 12, 18),
]
panels.append(row("Worldserver processes", 26))
panels += [
    series("CPU per worldserver (100 = one full core)", [target(f'procstat_cpu_usage{{{JOB},server=~"prod|sim"}}', "{{server}}")], 0, 27, unit="percent", minimum=0),
    series("Memory per worldserver (RSS)", [target(f'procstat_memory_rss{{{JOB},server=~"prod|sim"}}', "{{server}}")], 12, 27, unit="bytes", minimum=0),
    series("Threads", [target(f'procstat_num_threads{{{JOB},server=~"prod|sim"}}', "{{server}}")], 0, 35, w=24),
]
panels.append(row("Machine", 43))
panels += [
    series("CPU", [target(f'cpu_usage_user{{{JOB},cpu="cpu-total"}}', "user"), target(f'cpu_usage_system{{{JOB},cpu="cpu-total"}}', "system"),
                   target(f'cpu_usage_iowait{{{JOB},cpu="cpu-total"}}', "iowait")], 0, 44, unit="percent", stack=True, minimum=0, maximum=100),
    series("Memory", [target(f'mem_used{{{JOB}}}', "used"), target(f'mem_cached{{{JOB}}}', "cache"), target(f'mem_available{{{JOB}}}', "available")], 12, 44, unit="bytes"),
    series("Load average", [target(f'system_load1{{{JOB}}}', "1 min"), target(f'system_load5{{{JOB}}}', "5 min"), target(f'system_load15{{{JOB}}}', "15 min")], 0, 52),
    series("Network", [target(f'rate(net_bytes_recv{{{JOB},interface="ens2"}}[1m]) * 8', "in"), target(f'rate(net_bytes_sent{{{JOB},interface="ens2"}}[1m]) * 8', "out")],
           12, 52, unit="bps"),
    series("Disk used (/ holds /srv/forever)", [target(f'disk_used_percent{{{JOB},path="/"}}', "/")], 0, 60, unit="percent", minimum=0, maximum=100),
    series("Disk throughput", [target(f'rate(diskio_read_bytes{{{JOB}}}[1m])', "read {{name}}"), target(f'rate(diskio_write_bytes{{{JOB}}}[1m])', "write {{name}}")], 12, 60, unit="Bps"),
]

dashboard = {
    "uid": "forever-server", "title": "Forever server (Linux)", "tags": ["forever", "wow"], "timezone": "browser", "schemaVersion": 39, "version": 1,
    "refresh": "30s", "time": {"from": "now-6h", "to": "now"}, "editable": True, "graphTooltip": 1,
    "description": "The Forever prod and sim worldservers, the machine, and the sim's bots. Source: Telegraf job telegraf-forever.",
    "panels": panels, "templating": {"list": []}, "annotations": {"list": []},
}

if __name__ == "__main__":
    print(json.dumps(dashboard, indent=1))
