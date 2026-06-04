#!/usr/bin/env python3
"""Dream PC bridge: Bluetooth ThinkGear serial -> M5Stack USB serial + web console."""

from __future__ import annotations

import argparse
import csv
import copy
import json
import queue
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Iterable, Optional
from urllib.parse import parse_qs, urlparse

try:
    import serial
except ImportError as exc:
    print("pyserial is required. Install it with: python -m pip install pyserial", file=sys.stderr)
    raise exc


PC_BRIDGE_VERSION = "0.2.0"
THINKGEAR_MAX_PAYLOAD = 169
DEFAULT_POOR_SIGNAL = 200
STATE_PUSH_INTERVAL = 0.2
MIC_STATUS_STALE_MS = 3000
EEG_SEND_STALE_MS = 1500
SERIAL_RECONNECT_INTERVAL_S = 2.0
EEG_CONTROL_STALE_MS = 2500
EEG_CONTROL_POOR_SIGNAL_MAX = 120
CONFIG_STORE_NAME = "dream_config_store.json"
RUN_MODE_CODES = {"with_eeg": 0, "no_eeg": 1}
LIGHT_STRATEGY_CODES = {"eeg_realtime": 0, "palette_random": 1, "auto": 2}
CONTROL_ACTIONS = {
    "system_enable": "SYSTEM_ENABLE",
    "system_disable": "SYSTEM_DISABLE",
    "light_auto": "LIGHT_AUTO",
    "light_color": "LIGHT_COLOR",
    "light_off": "LIGHT_OFF",
    "relay_on": "RELAY_ON",
    "relay_off": "RELAY_OFF",
    "stepper_forward": "STEPPER_FORWARD",
    "stepper_backward": "STEPPER_BACKWARD",
    "stepper_stop": "STEPPER_STOP",
    "all_stop": "ALL_STOP",
    "fan_on": "FAN_ON",
    "fan_off": "FAN_OFF",
    "bubble_trigger": "BUBBLE_TRIGGER",
    "bubble_config": "BUBBLE_CONFIG",
    "vibration_enable": "VIBRATION_ENABLE",
    "vibration_disable": "VIBRATION_DISABLE",
    "run_mode": "RUN_MODE",
    "light_strategy": "LIGHT_STRATEGY",
    "palette_settings": "PALETTE_SETTINGS",
    "palette_node": "PALETTE_NODE",
}
EEG_POWER_FIELDS = (
    "delta",
    "theta",
    "lowAlpha",
    "highAlpha",
    "lowBeta",
    "highBeta",
    "lowGamma",
    "midGamma",
)


INDEX_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Dream Live Console</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f7f7f4;
      --surface: #fffefa;
      --surface-soft: #f0f1ed;
      --ink: #20211f;
      --muted: #6d7069;
      --line: #dedfd7;
      --accent: #3e6f6c;
      --accent-soft: #e4f0ed;
      --ok: #24845b;
      --warn: #9b6a17;
      --bad: #b53b35;
      --blue: #3f67d8;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: Inter, "Segoe UI", Arial, sans-serif;
      background: var(--bg);
      color: var(--ink);
      letter-spacing: 0;
    }
    .app {
      min-height: 100vh;
      display: grid;
      grid-template-columns: 280px minmax(0, 1fr);
    }
    aside {
      border-right: 1px solid var(--line);
      background: #f1f0eb;
      padding: 22px;
      display: flex;
      flex-direction: column;
      gap: 18px;
    }
    main {
      padding: 24px;
      display: grid;
      gap: 18px;
      align-content: start;
    }
    h1, h2, h3, p { margin: 0; }
    h1 {
      font-size: 30px;
      line-height: 1.1;
      font-weight: 760;
    }
    h2 {
      font-size: 15px;
      font-weight: 720;
    }
    h3 {
      color: var(--muted);
      font-size: 12px;
      font-weight: 700;
      text-transform: uppercase;
    }
    .brand {
      display: grid;
      gap: 8px;
    }
    .brand p, .subtle {
      color: var(--muted);
      font-size: 13px;
      line-height: 1.45;
    }
    .hero {
      display: flex;
      align-items: flex-start;
      justify-content: space-between;
      gap: 18px;
      padding-bottom: 4px;
    }
    .hero-copy {
      display: grid;
      gap: 8px;
    }
    .status-stack {
      display: grid;
      gap: 10px;
    }
    .status-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      min-height: 34px;
      border-bottom: 1px solid var(--line);
      font-size: 13px;
      color: var(--muted);
    }
    .status-row strong {
      color: var(--ink);
      text-align: right;
      overflow-wrap: anywhere;
    }
    .pill {
      min-width: 74px;
      height: 28px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      border: 1px solid var(--line);
      border-radius: 999px;
      padding: 0 10px;
      color: var(--muted);
      background: var(--surface);
      font-size: 12px;
      font-weight: 720;
      white-space: nowrap;
    }
    .pill.ok { color: var(--ok); background: #e8f4ee; border-color: #bad9c9; }
    .pill.warn { color: var(--warn); background: #fff4d8; border-color: #ead39a; }
    .pill.bad { color: var(--bad); background: #fae7e4; border-color: #ebb9b5; }
    .pill.neutral { color: var(--muted); background: var(--surface-soft); }
    .layout {
      display: grid;
      grid-template-columns: minmax(0, 1.16fr) minmax(360px, 0.84fr);
      gap: 18px;
      align-items: start;
    }
    .view-nav {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 8px;
      border-bottom: 1px solid var(--line);
      padding-bottom: 14px;
    }
    .view-nav button {
      min-height: 48px;
      text-align: left;
      padding: 8px 12px;
      display: grid;
      align-content: center;
      gap: 3px;
    }
    .view-nav button span {
      font-size: 13px;
      font-weight: 760;
    }
    .view-nav button small {
      color: var(--muted);
      font-size: 11px;
      font-weight: 650;
    }
    .view-nav button.is-selected {
      color: #f8f7f1;
      background: #343632;
      border-color: #343632;
    }
    .view-nav button.is-selected small { color: #d8d8d0; }
    .view-panel {
      display: grid;
      gap: 18px;
    }
    .view-panel[hidden] { display: none; }
    .mode-grid {
      display: grid;
      grid-template-columns: minmax(0, 0.92fr) minmax(360px, 1.08fr);
      gap: 18px;
      align-items: start;
    }
    .stack {
      display: grid;
      gap: 18px;
    }
    section {
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--surface);
      padding: 16px;
    }
    .section-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 14px;
    }
    .metric-grid {
      display: grid;
      gap: 10px;
    }
    .metric-grid.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
    .metric-grid.four { grid-template-columns: repeat(4, minmax(0, 1fr)); }
    .metric {
      min-height: 92px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #fbfaf6;
      padding: 12px;
      display: grid;
      align-content: space-between;
      gap: 10px;
    }
    .label {
      color: var(--muted);
      font-size: 12px;
      line-height: 1.25;
    }
    .value {
      font-size: 30px;
      line-height: 1;
      font-weight: 760;
      overflow-wrap: anywhere;
    }
    .value.small {
      font-size: 20px;
      line-height: 1.1;
    }
    .bar {
      height: 8px;
      overflow: hidden;
      border-radius: 999px;
      background: #e5e5de;
    }
    .bar > span {
      display: block;
      width: 0%;
      height: 100%;
      border-radius: inherit;
      background: var(--accent);
      transition: width 160ms linear;
    }
    .freq-grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 8px;
    }
    .freq {
      display: grid;
      gap: 5px;
      border-bottom: 1px solid var(--line);
      padding: 8px 2px 10px;
      font-size: 13px;
      color: var(--muted);
    }
    .freq strong {
      color: var(--ink);
      font-size: 16px;
      overflow-wrap: anywhere;
    }
    .control-panel {
      display: grid;
      gap: 14px;
    }
    .button-row {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 8px;
    }
    .button-row.two { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    .button-row.single { grid-template-columns: minmax(0, 1fr); }
    button, input[type="color"], input[type="range"], input[type="number"], input[type="text"], select {
      font: inherit;
    }
    button {
      min-height: 42px;
      border: 1px solid #d5d3ca;
      border-radius: 8px;
      background: #fbfaf6;
      color: var(--ink);
      font-size: 13px;
      font-weight: 720;
      cursor: pointer;
      box-shadow: 0 1px 0 rgba(35, 36, 32, 0.04);
      transition: background 120ms ease, border-color 120ms ease, color 120ms ease, box-shadow 120ms ease, transform 80ms ease, opacity 120ms ease;
    }
    button:hover:not(:disabled) {
      border-color: #aaa89e;
      background: #f1f0ea;
      box-shadow: 0 1px 2px rgba(35, 36, 32, 0.08);
    }
    button:active:not(:disabled) {
      transform: translateY(1px);
      box-shadow: none;
    }
    button:focus-visible {
      outline: 2px solid #b8aa94;
      outline-offset: 2px;
    }
    button.primary {
      color: #f8f7f1;
      background: #343632;
      border-color: #343632;
    }
    button.primary:hover:not(:disabled) {
      background: #282a27;
      border-color: #282a27;
    }
    button.good {
      color: #245b3c;
      background: #edf5ef;
      border-color: #bfd8c8;
    }
    button.good:hover:not(:disabled) {
      color: #1e4f34;
      background: #e2eee6;
      border-color: #9fc3ad;
    }
    button.danger {
      color: #96352f;
      background: #fbefed;
      border-color: #e3beb8;
    }
    button.danger:hover:not(:disabled) {
      color: #842c27;
      background: #f5e4e1;
      border-color: #d8a49d;
    }
    button.is-pending {
      opacity: 0.72;
    }
    button:disabled {
      cursor: not-allowed;
      color: #9b9d96;
      background: #eeeeea;
      border-color: #dedfd7;
      box-shadow: none;
    }
    .control-block {
      display: grid;
      gap: 10px;
      padding-top: 12px;
      border-top: 1px solid var(--line);
    }
    .control-title {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
    }
    .control-status {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    .control-status.single { grid-template-columns: minmax(0, 1fr); }
    .device-control-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 10px;
    }
    .device-control {
      display: grid;
      gap: 8px;
      min-width: 0;
    }
    .control-state {
      min-height: 34px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #fbfaf6;
      padding: 7px 10px;
      color: var(--muted);
      font-size: 12px;
    }
    .control-state strong {
      color: var(--ink);
      font-size: 13px;
      white-space: nowrap;
    }
    .control-state.is-hint {
      align-items: flex-start;
    }
    .control-state.is-hint strong {
      white-space: normal;
      text-align: right;
      overflow-wrap: anywhere;
      line-height: 1.35;
    }
    .step-row {
      display: grid;
      grid-template-columns: 44px minmax(0, 1fr) 82px;
      gap: 10px;
      align-items: center;
    }
    .flow-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    .flow-field {
      display: grid;
      gap: 5px;
      color: var(--muted);
      font-size: 12px;
      font-weight: 680;
    }
    .flow-field span {
      overflow-wrap: anywhere;
    }
    .flow-field small {
      color: var(--muted);
      font-size: 11px;
      line-height: 1.2;
      font-weight: 600;
    }
    .flow-preview {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    .mood-grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 8px;
    }
    .mood-button {
      --mood: linear-gradient(135deg, #5f7fcd, #7bd9c8);
      position: relative;
      min-height: 56px;
      padding: 8px 10px 8px 52px;
      display: grid;
      align-content: center;
      gap: 3px;
      overflow: hidden;
      text-align: left;
    }
    .mood-button::before {
      content: "";
      position: absolute;
      left: 12px;
      top: 50%;
      width: 28px;
      height: 28px;
      transform: translateY(-50%);
      border: 1px solid rgba(32, 33, 31, 0.16);
      border-radius: 8px;
      background: var(--mood);
      filter: saturate(0.65);
    }
    .mood-button span {
      position: relative;
      font-size: 13px;
      font-weight: 760;
      line-height: 1.1;
    }
    .mood-button small {
      position: relative;
      color: var(--muted);
      font-size: 11px;
      font-weight: 650;
      line-height: 1.1;
    }
    .mood-button.is-selected {
      border-color: #343632;
      box-shadow: 0 0 0 2px rgba(52, 54, 50, 0.08);
    }
    input[type="range"] { width: 100%; accent-color: var(--accent); }
    input[type="number"], input[type="text"], select {
      width: 100%;
      min-height: 38px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--surface);
      color: var(--ink);
      padding: 0 8px;
    }
    input[type="color"] {
      width: 100%;
      min-height: 38px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--surface);
      padding: 3px;
    }
    .segmented {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 6px;
    }
    .segmented.three { grid-template-columns: repeat(3, minmax(0, 1fr)); }
    .segmented button.is-selected {
      color: #f8f7f1;
      background: #343632;
      border-color: #343632;
    }
    .library-row {
      display: grid;
      grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
      gap: 8px;
      align-items: end;
    }
    .editor-actions {
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 8px;
    }
    .palette-preview {
      min-height: 40px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: linear-gradient(90deg, #4b7dff, #20c7bd, #ff5cc8, #ff8a4c);
    }
    .palette-list {
      max-height: 280px;
      overflow: auto;
      display: grid;
      gap: 8px;
      padding-right: 2px;
    }
    .palette-list-item {
      min-height: 58px;
      display: grid;
      grid-template-columns: 72px minmax(0, 1fr) auto;
      align-items: center;
      gap: 10px;
      text-align: left;
      padding: 8px 10px;
    }
    .palette-list-item.is-selected {
      border-color: #343632;
      box-shadow: 0 0 0 2px rgba(52, 54, 50, 0.08);
    }
    .palette-mini {
      width: 72px;
      height: 30px;
      border: 1px solid rgba(32, 33, 31, 0.14);
      border-radius: 6px;
      background: linear-gradient(90deg, #4b7dff, #20c7bd);
    }
    .palette-list-copy {
      display: grid;
      gap: 3px;
      min-width: 0;
    }
    .palette-list-copy strong {
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-size: 13px;
    }
    .palette-list-copy small {
      color: var(--muted);
      font-size: 11px;
      font-weight: 650;
    }
    .palette-editor {
      border-top: 1px solid var(--line);
      padding-top: 10px;
    }
    .palette-editor > summary {
      cursor: pointer;
      color: var(--muted);
      font-size: 13px;
      font-weight: 720;
      list-style: none;
    }
    .palette-editor > summary::-webkit-details-marker { display: none; }
    .palette-editor[open] > summary { color: var(--ink); }
    .palette-editor-body {
      display: grid;
      gap: 10px;
      padding-top: 10px;
    }
    .palette-stop {
      display: grid;
      grid-template-columns: 70px minmax(0, 1fr) 74px 42px;
      gap: 8px;
      align-items: center;
    }
    .palette-stop button {
      min-height: 38px;
      padding: 0;
    }
    .test-swatch {
      min-height: 34px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #eeeeea;
    }
    .notice {
      min-height: 22px;
      color: var(--muted);
      font-size: 13px;
      line-height: 1.4;
    }
    .log {
      height: 220px;
      overflow: auto;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #20211f;
      color: #f1f0ea;
      padding: 12px;
      font-family: Consolas, "Courier New", monospace;
      font-size: 12px;
      line-height: 1.45;
      white-space: pre-wrap;
    }
    .history-list {
      min-height: 176px;
      max-height: 260px;
      overflow: auto;
      display: grid;
      gap: 8px;
    }
    .history-item {
      min-height: 42px;
      display: grid;
      grid-template-columns: 84px minmax(0, 1fr) 72px;
      align-items: center;
      gap: 10px;
      border-bottom: 1px solid var(--line);
      color: var(--muted);
      font-size: 12px;
    }
    .history-item strong {
      color: var(--ink);
      font-size: 14px;
      overflow-wrap: anywhere;
    }
    @media (max-width: 1100px) {
      .app { grid-template-columns: 1fr; }
      aside { border-right: 0; border-bottom: 1px solid var(--line); }
      .layout { grid-template-columns: 1fr; }
      .mode-grid { grid-template-columns: 1fr; }
      .metric-grid.three, .metric-grid.four, .freq-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
    @media (max-width: 680px) {
      main, aside { padding: 14px; }
      .hero, .section-head, .control-title { align-items: flex-start; flex-direction: column; }
      .view-nav { grid-template-columns: 1fr; }
      .button-row, .button-row.two, .button-row.single, .control-status, .control-status.single, .device-control-grid, .flow-grid, .flow-preview, .mood-grid, .metric-grid.three, .metric-grid.four, .freq-grid { grid-template-columns: 1fr; }
      .step-row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="app">
    <aside>
      <div class="brand">
        <h1>Dream</h1>
        <p>现场实时控制台。页面只显示电脑串口、M5Stack 和 Microduino 回传的真实状态。</p>
      </div>
      <div class="status-stack">
        <h3>连接</h3>
        <div class="status-row"><span>网页</span><span id="webStatus" class="pill warn">连接中</span></div>
        <div class="status-row"><span>电脑串口</span><span id="sourceStatus" class="pill warn">等待</span></div>
        <div class="status-row"><span>M5Stack</span><span id="m5Status" class="pill warn">等待</span></div>
        <div class="status-row"><span>Microduino</span><span id="micStatus" class="pill warn">等待</span></div>
        <div class="status-row"><span>震动板</span><span id="vibrationStatus" class="pill warn">等待</span></div>
      </div>
      <div class="status-stack">
        <h3>端口</h3>
        <div class="status-row"><span>脑电 COM</span><strong id="sourcePort">--</strong></div>
        <div class="status-row"><span>M5 COM</span><strong id="targetPort">--</strong></div>
        <div class="status-row"><span>发送频率</span><strong id="sendRate">--</strong></div>
      </div>
      <div class="status-stack">
        <h3>安全</h3>
        <div class="status-row"><span>系统总开关</span><span id="systemEnabled" class="pill neutral">--</span></div>
        <div class="status-row"><span>最后错误</span><strong id="lastError">--</strong></div>
      </div>
    </aside>
    <main>
      <div class="hero">
        <div class="hero-copy">
          <h1>Dream 控制台</h1>
          <p class="subtle">先确定是否接入脑电，再进入现场控制；灯光和色板在独立容器中维护。</p>
        </div>
        <span id="eegStatus" class="pill warn">等待脑电</span>
      </div>
      <nav class="view-nav" aria-label="控制台界面">
        <button class="is-selected" data-view="modeView"><span>运行模式</span><small>有脑电 / 无脑电</small></button>
        <button data-view="controlView"><span>现场控制</span><small>泡泡、雾机、风扇、电机</small></button>
        <button data-view="paletteView"><span>色板与灯光</span><small>灯光策略和色板容器</small></button>
      </nav>

      <section id="modeView" class="view-panel">
        <div class="mode-grid">
          <section class="control-panel">
            <div class="section-head"><h2>选择作品运行方式</h2><span id="runModePill" class="pill neutral">--</span></div>
            <div class="segmented">
              <button data-run-mode="with_eeg">有脑电</button>
              <button data-run-mode="no_eeg">无脑电</button>
            </div>
            <div class="control-status single">
              <div class="control-state"><span>脑电可用性</span><strong id="eegDiagnosis">--</strong></div>
            </div>
            <div class="button-row two">
              <button data-view-jump="controlView">进入现场控制</button>
              <button data-view-jump="paletteView">进入色板与灯光</button>
            </div>
          </section>
          <div class="stack">
            <section>
              <div class="section-head"><h2>脑电实时数据</h2><span id="eegAge" class="pill neutral">-- ms</span></div>
              <div class="metric-grid three">
                <div class="metric"><div class="label">poorSignal</div><div class="value" id="poorSignal">--</div><div class="bar"><span id="poorSignalBar"></span></div></div>
                <div class="metric"><div class="label">attention</div><div class="value" id="attention">--</div><div class="bar"><span id="attentionBar"></span></div></div>
                <div class="metric"><div class="label">meditation</div><div class="value" id="meditation">--</div><div class="bar"><span id="meditationBar"></span></div></div>
              </div>
            </section>
            <section>
              <div class="section-head"><h2>EEG Power 频段</h2><span id="validPacketsPill" class="pill neutral">-- 包</span></div>
              <div class="freq-grid">
                <div class="freq"><span>delta</span><strong id="delta">--</strong></div>
                <div class="freq"><span>theta</span><strong id="theta">--</strong></div>
                <div class="freq"><span>lowAlpha</span><strong id="lowAlpha">--</strong></div>
                <div class="freq"><span>highAlpha</span><strong id="highAlpha">--</strong></div>
                <div class="freq"><span>lowBeta</span><strong id="lowBeta">--</strong></div>
                <div class="freq"><span>highBeta</span><strong id="highBeta">--</strong></div>
                <div class="freq"><span>lowGamma</span><strong id="lowGamma">--</strong></div>
                <div class="freq"><span>midGamma</span><strong id="midGamma">--</strong></div>
              </div>
            </section>
          </div>
        </div>
      </section>

      <section id="controlView" class="view-panel" hidden>
        <div class="layout">
          <div class="stack">
            <section>
              <div class="section-head"><h2>Microduino 执行状态</h2><span id="micAge" class="pill neutral">-- ms</span></div>
              <div class="metric-grid four">
                <div class="metric"><div class="label">灯光</div><div class="value small" id="lightMode">--</div><div class="label" id="lightLevel">level --</div></div>
                <div class="metric"><div class="label">雾机 / 风扇</div><div class="value small" id="relayState">--</div><div class="label" id="relayEnabled">--</div></div>
                <div class="metric"><div class="label">步进电机</div><div class="value small" id="stepperState">--</div><div class="label" id="stepperEnabled">--</div></div>
                <div class="metric"><div class="label">安全状态</div><div class="value small" id="safetyState">--</div><div class="label" id="lastAction">action --</div></div>
              </div>
            </section>
            <section>
              <div class="section-head"><h2>震动触发</h2><span id="vibrationSource" class="pill neutral">等待震动板</span></div>
              <div class="metric-grid four">
                <div class="metric"><div class="label">震动状态</div><div class="value small" id="vibrationState">--</div><div class="label" id="vibrationDetail">ESP32-S3</div></div>
                <div class="metric"><div class="label">触发次数</div><div class="value small" id="vibrationCount">--</div><div class="label">等同泡泡按钮</div></div>
                <div class="metric"><div class="label">距上一次</div><div class="value small" id="vibrationLast">--</div><div class="label">震动触发</div></div>
                <div class="metric"><div class="label">泡泡流程</div><div class="value small" id="bubbleState">--</div><div class="label" id="bubbleDetail">--</div></div>
              </div>
              <div class="control-status single">
                <div class="control-state"><span>震动触发开关</span><strong id="vibrationMode">--</strong></div>
              </div>
              <div class="button-row two">
                <button class="good" data-action="vibration_enable">启用震动触发</button>
                <button data-action="vibration_disable">停用震动触发</button>
              </div>
            </section>
            <section>
              <div class="section-head"><h2>链路统计</h2><span id="m5Age" class="pill neutral">-- ms</span></div>
              <div class="metric-grid four">
                <div class="metric"><div class="label">电脑发送帧</div><div class="value small" id="sentFrames">--</div><div class="label">USB -> M5</div></div>
                <div class="metric"><div class="label">ThinkGear 有效包</div><div class="value small" id="validPackets">--</div><div class="label" id="checksumErrors">checksum --</div></div>
                <div class="metric"><div class="label">M5 ESP-NOW</div><div class="value small" id="espNowStats">--</div><div class="label" id="m5Command">cmd --</div></div>
                <div class="metric"><div class="label">MIC 收包</div><div class="value small" id="micPackets">--</div><div class="label" id="micControl">control --</div></div>
              </div>
            </section>
          </div>
          <div class="stack">
            <section class="control-panel">
              <div class="section-head"><h2>现场控制</h2></div>
              <div class="button-row two">
                <button class="good" data-action="system_enable">系统开启</button>
                <button class="danger" data-action="system_disable">系统关闭</button>
              </div>
              <div class="button-row single">
                <button class="primary" data-action="bubble_trigger">保存并触发流程</button>
              </div>
              <div class="control-block">
                <div class="control-title"><h2>泡泡并行流程</h2><span id="bubbleConfigSummary" class="pill neutral">默认</span></div>
                <div class="library-row">
                  <label class="flow-field"><span>配置列表</span><select id="bubbleConfigSelect"></select></label>
                  <label class="flow-field"><span>配置名称</span><input id="bubbleConfigName" type="text" maxlength="60" value=""></label>
                </div>
                <div class="editor-actions">
                  <button id="bubbleConfigNew" type="button">新建</button>
                  <button id="bubbleConfigSave" type="button">保存</button>
                  <button id="bubbleConfigRename" type="button">重命名</button>
                  <button id="bubbleConfigDelete" class="danger" type="button">删除</button>
                </div>
                <div class="flow-grid">
                  <label class="flow-field"><span>反拉电机圈数</span><small>触发后立即启动</small><input class="bubble-config-input" id="bubbleReverseTurns" type="number" min="0" max="20" step="0.1" value="1"></label>
                  <label class="flow-field"><span>烟雾开启时间 秒</span><small>相对反转开始 0.0 秒</small><input class="bubble-config-input" id="bubbleFogStartSec" type="number" min="0" max="60" step="0.1" value="1.0"></label>
                  <label class="flow-field"><span>风扇/灯光开启时间 秒</span><small>相对反转开始 0.0 秒</small><input class="bubble-config-input" id="bubbleFanLightStartSec" type="number" min="0" max="60" step="0.1" value="5.0"></label>
                  <label class="flow-field"><span>回推启动时间 秒</span><small>相对反转开始；会等待反转完成</small><input class="bubble-config-input" id="bubbleForwardStartSec" type="number" min="0" max="60" step="0.1" value="9.0"></label>
                  <label class="flow-field"><span>回推电机圈数</span><small>反拉完成后才会执行</small><input class="bubble-config-input" id="bubbleForwardTurns" type="number" min="0" max="20" step="0.1" value="1"></label>
                  <label class="flow-field"><span>烟雾关闭时间 秒</span><small>相对反转开始 0.0 秒</small><input class="bubble-config-input" id="bubbleFogStopSec" type="number" min="0" max="60" step="0.1" value="12.7"></label>
                  <label class="flow-field"><span>风扇关闭时间 秒</span><small>相对反转开始 0.0 秒</small><input class="bubble-config-input" id="bubbleFanStopSec" type="number" min="0" max="60" step="0.1" value="13.7"></label>
                  <label class="flow-field"><span>灯光关闭时间 秒</span><small>相对反转开始 0.0 秒</small><input class="bubble-config-input" id="bubbleLightStopSec" type="number" min="0" max="60" step="0.1" value="16.7"></label>
                </div>
                <div class="flow-preview">
                  <div class="control-state"><span>电机轨道</span><strong id="bubbleMotorTimeline">--</strong></div>
                  <div class="control-state"><span>烟雾轨道</span><strong id="bubbleFogTimeline">--</strong></div>
                  <div class="control-state"><span>风扇轨道</span><strong id="bubbleFanTimeline">--</strong></div>
                  <div class="control-state"><span>灯光轨道</span><strong id="bubbleLightTimeline">--</strong></div>
                </div>
                <div class="button-row single">
                  <button data-action="bubble_config">仅下发当前流程配置</button>
                </div>
              </div>
              <div class="control-block">
                <div class="control-title"><h2>烟雾机 / 风扇</h2><span id="relayHint" class="pill neutral">--</span></div>
                <div class="device-control-grid">
                  <div class="device-control">
                    <div class="control-state"><span>烟雾机当前</span><strong id="fogCurrentState">--</strong></div>
                    <div class="button-row two">
                      <button class="good" data-action="relay_on">开启</button>
                      <button data-action="relay_off">停止</button>
                    </div>
                  </div>
                  <div class="device-control">
                    <div class="control-state"><span>风扇当前</span><strong id="fanCurrentState">--</strong></div>
                    <div class="button-row two">
                      <button class="good" data-action="fan_on">开启</button>
                      <button data-action="fan_off">停止</button>
                    </div>
                  </div>
                </div>
              </div>
              <div class="control-block">
                <div class="control-title"><h2>步进电机</h2><span id="stepperHint" class="pill neutral">--</span></div>
                <div class="step-row">
                  <span class="label">圈数</span>
                  <input id="stepperTurns" type="number" min="0.1" max="20" step="0.1" value="1" aria-label="步进电机圈数">
                  <span class="label" id="stepperStepsPreview">1600 步</span>
                </div>
                <div class="button-row">
                  <button class="primary" data-action="stepper_forward" data-turns="1">正转一圈</button>
                  <button class="primary" data-action="stepper_backward" data-turns="1">反转一圈</button>
                  <button data-action="stepper_stop">停止电机</button>
                </div>
                <div class="button-row two">
                  <button class="primary" data-action="stepper_forward">正转指定圈数</button>
                  <button class="primary" data-action="stepper_backward">反转指定圈数</button>
                </div>
              </div>
              <div id="notice" class="notice">等待真实串口连接。</div>
            </section>
            <section>
              <div class="section-head"><h2>事件日志</h2><button id="clearLog">清空本地显示</button></div>
              <div id="log" class="log">等待真实事件...</div>
            </section>
            <section>
              <div class="section-head"><h2>震动记录</h2><span id="vibrationHistoryCount" class="pill neutral">0 条</span></div>
              <div id="vibrationHistory" class="history-list">
                <div class="history-item"><span>--:--:--</span><strong>等待震动触发</strong><span>count --</span></div>
              </div>
            </section>
          </div>
        </div>
      </section>

      <section id="paletteView" class="view-panel" hidden>
        <div class="mode-grid">
          <section class="control-panel">
            <div class="section-head"><h2>灯光模式</h2><span id="lightMood" class="pill neutral">自动切换</span></div>
            <div class="control-status single">
              <div class="control-state"><span>当前状态</span><strong id="lightCurrentState">--</strong></div>
            </div>
            <div class="segmented three">
              <button data-light-strategy="eeg_realtime">脑电实时</button>
              <button data-light-strategy="palette_random">色板随机</button>
              <button data-light-strategy="auto">自动切换</button>
            </div>
            <div class="control-status single">
              <div class="control-state is-hint"><span>切换说明</span><strong id="lightStrategyHint">色板随机和自动切换可先保存，板端连接后同步。</strong></div>
            </div>
            <div class="button-row two">
              <button id="testEegLightColor" type="button">测试脑电灯光颜色</button>
              <button data-action="light_off">灯光关闭</button>
            </div>
            <div class="test-swatch" id="eegTestSwatch"></div>
          </section>
          <section class="control-panel">
            <div class="section-head"><h2>色板容器</h2><span id="paletteStatus" class="pill neutral">--</span></div>
            <div class="button-row two">
              <button id="paletteNew" type="button">新建色板</button>
              <button id="paletteDelete" class="danger" type="button">删除当前色板</button>
            </div>
            <select id="paletteSelect" hidden></select>
            <div id="paletteList" class="palette-list"></div>
            <details id="paletteEditor" class="palette-editor">
              <summary>编辑当前色板</summary>
              <div class="palette-editor-body">
                <label class="flow-field"><span>色板名称</span><input id="paletteName" type="text" maxlength="60" value="Dream 默认色板"></label>
                <div class="palette-preview" id="palettePreview"></div>
                <div class="flow-grid">
                  <label class="flow-field"><span>两灯时间差 秒</span><input id="paletteOffsetSec" type="number" min="0" max="60" step="0.1" value="1.0"></label>
                  <label class="flow-field"><span>流水灯周期 秒</span><input id="paletteFlowSec" type="number" min="0.5" max="60" step="0.1" value="6.0"></label>
                </div>
                <div id="paletteStops" class="metric-grid"></div>
                <div class="editor-actions">
                  <button id="paletteSave" type="button">保存</button>
                  <button id="paletteRename" type="button">重命名</button>
                  <button id="paletteAddStop" type="button">增加色标</button>
                  <button id="paletteCloseEditor" type="button">收起</button>
                </div>
              </div>
            </details>
          </section>
        </div>
      </section>
    </main>
  </div>
  <script>
    const lightModes = ["OFF", "IDLE", "EEG", "BAD", "TIMEOUT", "MANUAL"];
    const stepperStates = ["DISABLED", "IDLE", "MOVING", "BREATH", "LIMIT", "FAULT"];
    const relayStates = ["OFF", "ARMING", "ON", "COOL", "FAULT"];
    const bubbleStates = ["IDLE", "REV", "FOG", "BLOW", "FWD", "FOG_HOLD", "WIND", "LIGHT"];
    const safetyStates = ["NORMAL", "SIGNAL", "TIMEOUT", "ESTOP", "FAULT"];
    const controlActions = ["NONE", "SYSTEM_ENABLE", "SYSTEM_DISABLE", "LIGHT_AUTO", "LIGHT_COLOR", "LIGHT_OFF", "RELAY_ON", "RELAY_OFF", "STEPPER_FORWARD", "STEPPER_BACKWARD", "STEPPER_STOP", "ALL_STOP", "FAN_ON", "FAN_OFF", "BUBBLE_TRIGGER", "BUBBLE_CONFIG", "VIB_ENABLE", "VIB_DISABLE"];
    const EEG_POWER_FIELDS = ["delta", "theta", "lowAlpha", "highAlpha", "lowBeta", "highBeta", "lowGamma", "midGamma"];
    const STEPPER_STEPS_PER_REV = 1600;
    const ids = {};
    const buttonLabels = new WeakMap();
    let localNotice = "";
    let suppressLogRender = false;
    let commandFeedbackUntil = 0;
    let commandFeedbackTimer = 0;
    let latestCommandReady = false;
    let latestOutputReady = false;
    let latestM5Seen = false;
    let latestMicSeen = false;
    let latestSystemEnabled = false;
    let latestBubbleActive = false;
    let latestRelayOutputEnabled = false;
    let latestFogOn = false;
    let latestFanOn = false;
    let latestVibrationEnabled = true;
    let lastVibrationTriggerCount = null;
    let configStore = null;
    let paletteDraft = null;
    let configStoreSignature = "";
    const vibrationHistory = [];
    const STEPPER_SINGLE_TARGET = 1;
    const BUBBLE_CONFIG_INPUT_IDS = [
      "bubbleReverseTurns",
      "bubbleFogStartSec",
      "bubbleFanLightStartSec",
      "bubbleForwardStartSec",
      "bubbleForwardTurns",
      "bubbleFogStopSec",
      "bubbleFanStopSec",
      "bubbleLightStopSec"
    ];
    const BUBBLE_TIMELINE_INPUT_IDS = [
      "bubbleFogStartSec",
      "bubbleFanLightStartSec",
      "bubbleForwardStartSec",
      "bubbleFogStopSec",
      "bubbleFanStopSec",
      "bubbleLightStopSec"
    ];
    const systemOutputActions = new Set([
      "light_auto",
      "relay_on",
      "relay_off",
      "fan_on",
      "fan_off",
      "stepper_forward",
      "stepper_backward",
      "stepper_stop",
      "bubble_trigger"
    ]);
    const bubbleLockedActions = new Set(["stepper_forward", "stepper_backward", "stepper_stop", "relay_on", "relay_off", "fan_on", "fan_off", "bubble_trigger", "bubble_config"]);
    document.querySelectorAll("[id]").forEach((el) => ids[el.id] = el);
    document.querySelectorAll("button[data-action], #clearLog").forEach((button) => buttonLabels.set(button, button.textContent));
    const lightPresetButtons = Array.from(document.querySelectorAll(".mood-button"));
    const runModeButtons = Array.from(document.querySelectorAll("[data-run-mode]"));
    const lightStrategyButtons = Array.from(document.querySelectorAll("[data-light-strategy]"));
    const lightOffButton = document.querySelector("[data-action='light_off']");
    const viewButtons = Array.from(document.querySelectorAll("[data-view]"));
    const viewPanels = Array.from(document.querySelectorAll(".view-panel"));
    const LIGHT_STRATEGY_LABELS = {
      eeg_realtime: "脑电实时",
      palette_random: "色板随机",
      auto: "自动切换"
    };

    const setText = (id, value) => { ids[id].textContent = value ?? "--"; };
    const realText = (seen, value, suffix = "") => seen && value !== undefined && value !== null ? `${value}${suffix}` : "--";
    const modeText = (seen, values, index) => seen ? (values[index] ?? `UNKNOWN ${index}`) : "--";
    const actionText = (seen, index) => seen ? `action ${controlActions[index] ?? `UNKNOWN ${index}`}` : "action --";
    const rgbwText = (values) => Array.isArray(values) ? values.join("/") : "--/--/--/--";
    const onOffText = (seen, active, detail = "") => !seen ? "--" : `${active ? "开启" : "关闭"}${detail ? ` · ${detail}` : ""}`;
    const setBar = (id, seen, value, max = 100, invert = false) => {
      if (!seen) {
        ids[id].style.width = "0%";
        return;
      }
      const pct = Math.max(0, Math.min(100, Number(value || 0) * 100 / max));
      ids[id].style.width = (invert ? 100 - pct : pct) + "%";
    };
    const pill = (el, state, text) => {
      el.className = "pill " + state;
      el.textContent = text;
    };
    const lightStrategyLabel = (strategy) => LIGHT_STRATEGY_LABELS[strategy] || LIGHT_STRATEGY_LABELS.auto;
    const RUN_MODE_LABELS = {
      with_eeg: "有脑电",
      no_eeg: "无脑电"
    };
    const applyRunModeSelection = (runMode = "with_eeg", pending = false) => {
      const normalized = RUN_MODE_LABELS[runMode] ? runMode : "with_eeg";
      runModeButtons.forEach((button) => {
        button.classList.toggle("is-selected", button.dataset.runMode === normalized);
        button.disabled = pending;
      });
      pill(
        ids.runModePill,
        pending ? "warn" : (normalized === "with_eeg" ? "ok" : "neutral"),
        pending ? "保存中" : RUN_MODE_LABELS[normalized]
      );
    };
    const applyLightStrategySelection = (strategy = "auto") => {
      lightStrategyButtons.forEach((button) => {
        button.classList.toggle("is-selected", button.dataset.lightStrategy === strategy);
      });
      pill(ids.lightMood, strategy === "eeg_realtime" ? "ok" : (strategy === "palette_random" ? "neutral" : "warn"), lightStrategyLabel(strategy));
    };
    const setNotice = (text) => {
      localNotice = text;
      ids.notice.textContent = text;
    };
    const normalizeHex = (value) => value && value.startsWith("#") ? value : `#${value || "000000"}`;
    const escapeHtml = (text) => String(text ?? "").replace(/[&<>"']/g, (ch) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;"}[ch]));
    const clamp = (value, min, max, fallback = min) => {
      const number = Number(value);
      return Math.max(min, Math.min(max, Number.isFinite(number) ? number : fallback));
    };
    const selectedPalette = () => {
      if (!configStore?.palettes?.length) return null;
      return configStore.palettes.find((item) => item.id === configStore.app.selectedPaletteId) || configStore.palettes[0];
    };
    const selectedBubbleConfig = () => {
      if (!configStore?.bubbleConfigs?.length) return null;
      return configStore.bubbleConfigs.find((item) => item.id === configStore.app.selectedBubbleConfigId) || null;
    };
    const editingConfigInput = () => {
      const active = document.activeElement;
      return Boolean(active && (active.closest("#paletteEditor") || active.id?.startsWith("bubbleConfig")));
    };
    const showView = (viewId) => {
      viewPanels.forEach((panel) => panel.hidden = panel.id !== viewId);
      viewButtons.forEach((button) => button.classList.toggle("is-selected", button.dataset.view === viewId));
    };
    const stepperTurns = (trigger) => {
      const raw = Number(trigger?.dataset.turns || ids.stepperTurns.value || 1);
      return Math.max(0.1, Math.min(20, Number.isFinite(raw) ? raw : 1));
    };
    const stepperSteps = (trigger) => Math.max(1, Math.min(65535, Math.round(stepperTurns(trigger) * STEPPER_STEPS_PER_REV)));
    const updateStepperPreview = () => {
      setText("stepperStepsPreview", `${stepperSteps(null)} 步`);
    };
    const readNumber = (id, fallback, min, max) => {
      const raw = Number(ids[id]?.value ?? fallback);
      const value = Number.isFinite(raw) ? raw : fallback;
      return Math.max(min, Math.min(max, value));
    };
    const turnsToSteps = (turns) => Math.max(0, Math.min(65535, Math.round(turns * STEPPER_STEPS_PER_REV)));
    const timelineSeconds = (id, fallback) => Math.round(readNumber(id, fallback, 0, 60) * 10) / 10;
    const secondsToMs = (seconds) => Math.max(0, Math.min(60000, Math.round(seconds * 10) * 100));
    const timelineLabel = (seconds) => Number(seconds).toFixed(1);
    const bubbleConfigValues = () => {
      return {
        reverseTurns: readNumber("bubbleReverseTurns", 1, 0, 20),
        forwardTurns: readNumber("bubbleForwardTurns", 1, 0, 20),
        fogStartSec: timelineSeconds("bubbleFogStartSec", 1.0),
        fanLightStartSec: timelineSeconds("bubbleFanLightStartSec", 5.0),
        forwardStartSec: timelineSeconds("bubbleForwardStartSec", 9.0),
        fogStopSec: timelineSeconds("bubbleFogStopSec", 12.7),
        fanStopSec: timelineSeconds("bubbleFanStopSec", 13.7),
        lightStopSec: timelineSeconds("bubbleLightStopSec", 16.7)
      };
    };
    const bubbleConfigArgs = () => {
      const config = bubbleConfigValues();
      return [
        turnsToSteps(config.reverseTurns),
        secondsToMs(config.fogStartSec),
        secondsToMs(config.fanLightStartSec),
        secondsToMs(config.forwardStartSec),
        turnsToSteps(config.forwardTurns),
        secondsToMs(config.fogStopSec),
        secondsToMs(config.fanStopSec),
        secondsToMs(config.lightStopSec)
      ];
    };
    const updateBubbleConfigPreview = () => {
      const config = bubbleConfigValues();
      const args = bubbleConfigArgs();
      setText("bubbleConfigSummary", `基准 0.0s · 步进 0.1s`);
      setText("bubbleMotorTimeline", `0.0s 反拉 ${args[0]} 步；${timelineLabel(config.forwardStartSec)}s 回推 ${args[4]} 步`);
      setText("bubbleFogTimeline", `${timelineLabel(config.fogStartSec)}s 开；${timelineLabel(config.fogStopSec)}s 关`);
      setText("bubbleFanTimeline", `${timelineLabel(config.fanLightStartSec)}s 开；${timelineLabel(config.fanStopSec)}s 关`);
      setText("bubbleLightTimeline", `${timelineLabel(config.fanLightStartSec)}s 开；${timelineLabel(config.lightStopSec)}s 关`);
    };
    const applyBubbleValues = (values) => {
      if (!values) return;
      ids.bubbleReverseTurns.value = values.reverseTurns ?? 1;
      ids.bubbleForwardTurns.value = values.forwardTurns ?? 1;
      ids.bubbleFogStartSec.value = Number(values.fogStartSec ?? 1).toFixed(1);
      ids.bubbleFanLightStartSec.value = Number(values.fanLightStartSec ?? 5).toFixed(1);
      ids.bubbleForwardStartSec.value = Number(values.forwardStartSec ?? 9).toFixed(1);
      ids.bubbleFogStopSec.value = Number(values.fogStopSec ?? 12.7).toFixed(1);
      ids.bubbleFanStopSec.value = Number(values.fanStopSec ?? 13.7).toFixed(1);
      ids.bubbleLightStopSec.value = Number(values.lightStopSec ?? 16.7).toFixed(1);
      updateBubbleConfigPreview();
    };
    const paletteGradient = (palette) => {
      const stops = [...(palette?.stops || [])].sort((a, b) => a.position - b.position);
      if (!stops.length) return "linear-gradient(90deg, #4b7dff, #20c7bd)";
      return `linear-gradient(90deg, ${stops.map((stop) => `${stop.color} ${stop.position}%`).join(", ")})`;
    };
    const nextPaletteName = () => {
      const existing = new Set((configStore?.palettes || []).map((item) => item.name));
      let index = (configStore?.palettes?.length || 0) + 1;
      let name = `新色板 ${index}`;
      while (existing.has(name)) {
        index += 1;
        name = `新色板 ${index}`;
      }
      return name;
    };
    const currentPaletteDraft = () => {
      const stops = Array.from(document.querySelectorAll(".palette-stop")).map((row) => ({
        position: Math.round(clamp(row.querySelector("[data-stop-position]").value, 0, 100, 0)),
        color: normalizeHex(row.querySelector("[data-stop-color]").value),
        white: Math.round(clamp(row.querySelector("[data-stop-white]").value, 0, 255, 0))
      })).sort((a, b) => a.position - b.position);
      return {
        id: ids.paletteSelect.value || paletteDraft?.id || "",
        name: ids.paletteName.value.trim() || "未命名色板",
        stops: stops.length ? stops : [{position: 0, color: "#4b7dff", white: 0}, {position: 100, color: "#ff5cc8", white: 0}],
        twoLightOffsetSec: Math.round(clamp(ids.paletteOffsetSec.value, 0, 60, 1) * 10) / 10,
        flowPeriodSec: Math.round(clamp(ids.paletteFlowSec.value, 0.5, 60, 6) * 10) / 10
      };
    };
    const renderPaletteStops = (palette) => {
      ids.paletteStops.innerHTML = "";
      (palette?.stops || []).forEach((stop) => {
        const row = document.createElement("div");
        row.className = "palette-stop";
        row.innerHTML = `
          <input data-stop-position type="number" min="0" max="100" step="1" value="${Number(stop.position || 0)}" aria-label="色标位置">
          <input data-stop-color type="color" value="${escapeHtml(stop.color || "#4b7dff")}" aria-label="色标颜色">
          <input data-stop-white type="number" min="0" max="255" step="1" value="${Number(stop.white || 0)}" aria-label="白光">
          <button type="button" title="删除色标">×</button>
        `;
        row.querySelector("button").addEventListener("click", () => {
          row.remove();
          ids.palettePreview.style.background = paletteGradient(currentPaletteDraft());
        });
        row.querySelectorAll("input").forEach((input) => input.addEventListener("input", () => {
          ids.palettePreview.style.background = paletteGradient(currentPaletteDraft());
        }));
        ids.paletteStops.appendChild(row);
      });
      ids.palettePreview.style.background = paletteGradient(palette);
    };
    const renderPaletteList = (store) => {
      if (!ids.paletteList) return;
      const selectedId = store?.app?.selectedPaletteId || "";
      ids.paletteList.innerHTML = (store?.palettes || []).map((item) => {
        const selected = item.id === selectedId;
        return `
          <button class="palette-list-item ${selected ? "is-selected" : ""}" type="button" data-palette-id="${escapeHtml(item.id)}">
            <span class="palette-mini" style="background: ${escapeHtml(paletteGradient(item))}"></span>
            <span class="palette-list-copy">
              <strong>${escapeHtml(item.name)}</strong>
              <small>${(item.stops || []).length} 个色标 · 差 ${Number(item.twoLightOffsetSec || 0).toFixed(1)}s · 周期 ${Number(item.flowPeriodSec || 0).toFixed(1)}s</small>
            </span>
            <span class="pill ${selected ? "ok" : "neutral"}">${selected ? "当前" : "选择"}</span>
          </button>
        `;
      }).join("");
      ids.paletteList.querySelectorAll("[data-palette-id]").forEach((button) => {
        button.addEventListener("click", async () => {
          try {
            await postConfig({kind: "palette", action: "select", id: button.dataset.paletteId});
            ids.paletteEditor.open = false;
          } catch (error) {
            setNotice(`色板加载失败：${error.message || error}`);
          }
        });
      });
    };
    const renderConfigStore = (store, keepDraft = false) => {
      if (!store) return;
      configStore = store;
      const palette = keepDraft && paletteDraft ? paletteDraft : selectedPalette();
      paletteDraft = JSON.parse(JSON.stringify(palette || {}));
      ids.paletteSelect.innerHTML = (store.palettes || []).map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join("");
      ids.paletteSelect.value = store.app.selectedPaletteId || palette?.id || "";
      renderPaletteList(store);
      ids.paletteName.value = palette?.name || "";
      ids.paletteOffsetSec.value = Number(palette?.twoLightOffsetSec ?? 1).toFixed(1);
      ids.paletteFlowSec.value = Number(palette?.flowPeriodSec ?? 6).toFixed(1);
      renderPaletteStops(palette);
      pill(ids.paletteStatus, "ok", palette?.name || "--");

      ids.bubbleConfigSelect.innerHTML = '<option value="">未选择</option>' + (store.bubbleConfigs || []).map((item) => `<option value="${escapeHtml(item.id)}">${escapeHtml(item.name)}</option>`).join("");
      ids.bubbleConfigSelect.value = store.app.selectedBubbleConfigId || "";
      const bubble = selectedBubbleConfig();
      ids.bubbleConfigName.value = bubble?.name || "";
      if (bubble) applyBubbleValues(bubble.values);
      pill(ids.bubbleLibraryStatus, bubble ? "ok" : "neutral", bubble ? bubble.name : "未选择");

      const runMode = store.app.runMode || "with_eeg";
      applyRunModeSelection(runMode);
      const strategy = store.app.lightStrategy || "auto";
      applyLightStrategySelection(strategy);
    };
    const postConfig = async (payload) => {
      const response = await fetch("/api/config", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify(payload)
      });
      const data = await response.json();
      if (!response.ok || !data.ok) throw new Error(data.error || response.status);
      renderConfigStore(data.store);
      if (data.warnings?.length) {
        setNotice(`配置已保存；板端同步等待串口：${data.warnings[0]}`);
      } else {
        setNotice("配置已保存并同步。");
      }
      return data;
    };
    const commandArgs = (action, trigger) => {
      if (action === "light_color") {
        const hex = normalizeHex(trigger?.dataset.color).slice(1);
        return [
          parseInt(hex.slice(0, 2), 16),
          parseInt(hex.slice(2, 4), 16),
          parseInt(hex.slice(4, 6), 16),
          Number(trigger?.dataset.white || 0)
        ];
      }
      if (action === "stepper_forward" || action === "stepper_backward") {
        return [stepperSteps(trigger), STEPPER_SINGLE_TARGET, 0, 0];
      }
      if (action === "stepper_stop") {
        return [0, STEPPER_SINGLE_TARGET, 0, 0];
      }
      if (action === "bubble_config") {
        return bubbleConfigArgs();
      }
      return [0, 0, 0, 0];
    };
    const setCommandFeedback = (state, text, duration = 1600) => {
      if (ids.commandStatus) {
        pill(ids.commandStatus, state, text);
      }
      commandFeedbackUntil = Date.now() + duration;
      window.clearTimeout(commandFeedbackTimer);
      commandFeedbackTimer = window.setTimeout(() => setCommandReady(latestCommandReady), duration);
    };
    const canUseButton = (button) => {
      const action = button.dataset.action || "";
      if (!latestCommandReady) return false;
      if (action === "system_enable" || action === "system_disable") {
        return true;
      }
      if (action === "vibration_enable") {
        return latestM5Seen && !latestVibrationEnabled;
      }
      if (action === "vibration_disable") {
        return latestM5Seen && latestVibrationEnabled;
      }
      if (!latestMicSeen) return false;
      if (action === "light_color" || action === "light_off") {
        return latestSystemEnabled;
      }
      if (action === "relay_on") {
        return latestSystemEnabled && latestRelayOutputEnabled && !latestBubbleActive && !latestFogOn;
      }
      if (action === "relay_off") {
        return latestRelayOutputEnabled && !latestBubbleActive && latestFogOn;
      }
      if (action === "fan_on") {
        return latestSystemEnabled && latestRelayOutputEnabled && !latestBubbleActive && !latestFanOn;
      }
      if (action === "fan_off") {
        return latestRelayOutputEnabled && !latestBubbleActive && latestFanOn;
      }
      if (action === "bubble_trigger") {
        return latestSystemEnabled && latestRelayOutputEnabled && !latestBubbleActive;
      }
      if (bubbleLockedActions.has(action) && latestBubbleActive) {
        return false;
      }
      return !systemOutputActions.has(action) || latestSystemEnabled;
    };
    const setButtonPending = (button, pending) => {
      button.classList.toggle("is-pending", pending);
      button.disabled = pending || !canUseButton(button);
      button.setAttribute("aria-busy", pending ? "true" : "false");
    };
    const setCommandReady = (enabled) => {
      latestCommandReady = enabled;
      if (!ids.commandStatus) return;
      if (Date.now() < commandFeedbackUntil) return;
      pill(ids.commandStatus, enabled ? "ok" : "warn", enabled ? "就绪" : "等待串口");
    };
    const setControlAvailability = (commandReady, micSeen, systemEnabled, bubbleActive, relayOutputEnabled = false, fogOn = false, fanOn = false) => {
      latestOutputReady = Boolean(commandReady && micSeen && systemEnabled);
      latestMicSeen = micSeen;
      latestSystemEnabled = systemEnabled;
      latestBubbleActive = bubbleActive;
      latestRelayOutputEnabled = relayOutputEnabled;
      latestFogOn = fogOn;
      latestFanOn = fanOn;
      document.querySelectorAll("button[data-action]").forEach((button) => {
        const pending = button.classList.contains("is-pending");
        latestCommandReady = commandReady;
        button.disabled = pending || !canUseButton(button);
      });
      ids.stepperTurns.disabled = !commandReady || !micSeen || !systemEnabled || bubbleActive;
      BUBBLE_CONFIG_INPUT_IDS.forEach((id) => {
        if (ids[id]) ids[id].disabled = bubbleActive;
      });
    };
    const setLightModeAvailability = (state, commandReady, micSeen) => {
      const runMode = configStore?.app?.runMode || "with_eeg";
      const eegUsable = Boolean(state?.eeg?.usable);
      const eegDiagnosis = state?.eeg?.diagnosis || "脑电信号不可用";
      lightStrategyButtons.forEach((button) => {
        const strategy = button.dataset.lightStrategy;
        const blocked = strategy === "eeg_realtime" && runMode === "no_eeg";
        button.disabled = blocked;
        button.title = blocked
          ? "当前整体模式是无脑电，不能选择脑电实时。"
          : "灯光策略会先保存到本地配置；板端连接后会同步。";
      });
      if (ids.testEegLightColor) {
        let reason = "";
        if (!eegUsable) {
          reason = eegDiagnosis;
        } else if (!commandReady) {
          reason = "等待 M5Stack 串口打开后才能下发测试灯色。";
        }
        ids.testEegLightColor.disabled = Boolean(reason);
        ids.testEegLightColor.title = reason || "根据当前脑电和当前色板，下发一次灯光颜色测试。";
      }
      if (lightOffButton) {
        let offReason = "";
        if (!commandReady) {
          offReason = "等待 M5Stack 串口打开后才能关闭灯光。";
        } else if (!micSeen) {
          offReason = "等待 Microduino 状态回传后才能关闭灯光。";
        } else if (!state?.mic?.systemEnabled) {
          offReason = "系统关闭状态下无需再次关闭灯光。";
        }
        lightOffButton.title = offReason || "向 Microduino 下发灯光关闭命令。";
      }
      let hint = "";
      if (runMode === "no_eeg") {
        hint = "无脑电模式下脑电实时不可选；色板随机和自动切换可先保存，板端连接后同步。";
      } else if (!eegUsable) {
        hint = `有脑电模式，但当前${eegDiagnosis}；自动切换会在不可用时走色板随机。`;
      } else if (!commandReady) {
        hint = "脑电信号可用；当前 M5Stack 串口未打开，策略会先保存到本地配置。";
      } else {
        hint = "脑电信号可用；策略切换会保存并同步到板端。";
      }
      setText("lightStrategyHint", hint);
    };
    const renderLogs = (logs) => {
      if (suppressLogRender) return;
      if (logs && logs.length) {
        ids.log.textContent = logs.join("\n");
      } else {
        ids.log.textContent = "等待真实事件...";
      }
    };
    const renderVibrationHistory = () => {
      setText("vibrationHistoryCount", `${vibrationHistory.length} 条`);
      if (!vibrationHistory.length) {
        ids.vibrationHistory.innerHTML = '<div class="history-item"><span>--:--:--</span><strong>等待震动触发</strong><span>count --</span></div>';
        return;
      }
      ids.vibrationHistory.innerHTML = vibrationHistory.map((item) => (
        `<div class="history-item"><span>${item.time}</span><strong>${item.label}</strong><span>count ${item.count}</span></div>`
      )).join("");
    };
    const updateVibrationHistory = (state, vibrationSeen) => {
      if (!vibrationSeen) return;
      const count = Number(state.m5.vibrationTriggerCount || 0);
      if (lastVibrationTriggerCount === null) {
        lastVibrationTriggerCount = count;
        return;
      }
      if (count <= lastVibrationTriggerCount) return;
      const now = new Date();
      vibrationHistory.unshift({
        time: now.toLocaleTimeString("zh-CN", {hour12: false}),
        label: state.m5.vibrationDetected ? "检测到震动" : "震动触发",
        count
      });
      vibrationHistory.splice(20);
      lastVibrationTriggerCount = count;
      renderVibrationHistory();
    };
    const postCommand = async (action, args) => {
      const response = await fetch("/api/command", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({action, args})
      });
      const payload = await response.json();
      if (!response.ok) {
        throw new Error(payload.error || response.status);
      }
      return payload;
    };
    const sendCommand = async (button) => {
      const action = button.dataset.action;
      const label = buttonLabels.get(button) || action;
      setButtonPending(button, true);
      setCommandFeedback("warn", "发送中", 30000);
      setNotice(`正在发送：${label}`);
      try {
        if (action === "bubble_trigger") {
          const configPayload = await postCommand("bubble_config", bubbleConfigArgs());
          const triggerPayload = await postCommand(action, commandArgs(action, button));
          setCommandFeedback("ok", "已发送");
          setNotice(`配置已保存并触发：${configPayload.command}；${triggerPayload.command}`);
          return;
        }
        const payload = await postCommand(action, commandArgs(action, button));
        setCommandFeedback("ok", "已发送");
        setNotice(`命令已交给串口队列：${payload.command}`);
      } catch (error) {
        setCommandFeedback("bad", "失败");
        setNotice(`命令未发送：${error.message || error}`);
      } finally {
        setButtonPending(button, false);
      }
    };
    document.querySelectorAll("button[data-action]").forEach((button) => {
      button.addEventListener("click", () => sendCommand(button));
    });
    viewButtons.forEach((button) => {
      button.addEventListener("click", () => showView(button.dataset.view));
    });
    document.querySelectorAll("[data-view-jump]").forEach((button) => {
      button.addEventListener("click", () => showView(button.dataset.viewJump));
    });
    runModeButtons.forEach((button) => {
      button.addEventListener("click", async () => {
        const nextRunMode = button.dataset.runMode;
        const previousRunMode = configStore?.app?.runMode;
        if (configStore && nextRunMode === previousRunMode) {
          applyRunModeSelection(nextRunMode);
          return;
        }
        applyRunModeSelection(nextRunMode, true);
        setNotice(`整体运行模式已选择：${RUN_MODE_LABELS[nextRunMode] || nextRunMode}。正在保存配置...`);
        try {
          await postConfig({kind: "app", action: "update", app: {runMode: nextRunMode}});
        } catch (error) {
          applyRunModeSelection(previousRunMode || "with_eeg");
          setNotice(`整体模式保存失败：${error.message || error}`);
        }
      });
    });
    lightStrategyButtons.forEach((button) => {
      button.addEventListener("click", async () => {
        const strategy = button.dataset.lightStrategy;
        if (strategy === "eeg_realtime" && configStore?.app?.runMode === "no_eeg") {
          setNotice("无脑电整体模式下不能使用脑电实时灯光。");
          return;
        }
        const previousStrategy = configStore?.app?.lightStrategy || "auto";
        applyLightStrategySelection(strategy);
        setNotice(`灯光模式已选择：${lightStrategyLabel(strategy)}。正在保存配置...`);
        try {
          await postConfig({kind: "app", action: "update", app: {lightStrategy: strategy}});
        } catch (error) {
          applyLightStrategySelection(previousStrategy);
          setNotice(`灯光模式保存失败：${error.message || error}`);
        }
      });
    });
    ids.paletteSelect.addEventListener("change", async () => {
      try {
        await postConfig({kind: "palette", action: "select", id: ids.paletteSelect.value});
      } catch (error) {
        setNotice(`色板加载失败：${error.message || error}`);
      }
    });
    ids.paletteAddStop.addEventListener("click", () => {
      const palette = currentPaletteDraft();
      palette.stops.push({position: 50, color: "#ffffff", white: 0});
      renderPaletteStops(palette);
    });
    ids.paletteNew.addEventListener("click", async () => {
      try {
        const item = {
          name: nextPaletteName(),
          stops: [
            {position: 0, color: "#4b7dff", white: 0},
            {position: 100, color: "#ff5cc8", white: 0}
          ],
          twoLightOffsetSec: 1.0,
          flowPeriodSec: 6.0
        };
        await postConfig({kind: "palette", action: "create", item});
        ids.paletteEditor.open = true;
        setNotice(`已新建：${item.name}`);
      } catch (error) {
        setNotice(`新建色板失败：${error.message || error}`);
      }
    });
    ids.paletteSave.addEventListener("click", async () => {
      try {
        await postConfig({kind: "palette", action: "save", id: ids.paletteSelect.value, item: currentPaletteDraft()});
      } catch (error) {
        setNotice(`色板保存失败：${error.message || error}`);
      }
    });
    ids.paletteRename.addEventListener("click", async () => {
      try {
        await postConfig({kind: "palette", action: "rename", id: ids.paletteSelect.value, name: ids.paletteName.value.trim()});
      } catch (error) {
        setNotice(`色板重命名失败：${error.message || error}`);
      }
    });
    ids.paletteDelete.addEventListener("click", async () => {
      try {
        await postConfig({kind: "palette", action: "delete", id: ids.paletteSelect.value});
        ids.paletteEditor.open = false;
      } catch (error) {
        setNotice(`色板删除失败：${error.message || error}`);
      }
    });
    ids.paletteCloseEditor.addEventListener("click", () => {
      ids.paletteEditor.open = false;
    });
    ids.bubbleConfigSelect.addEventListener("change", async () => {
      try {
        await postConfig({kind: "bubble", action: "select", id: ids.bubbleConfigSelect.value, sync: false});
      } catch (error) {
        setNotice(`泡泡配置加载失败：${error.message || error}`);
      }
    });
    ids.bubbleConfigNew.addEventListener("click", async () => {
      try {
        await postConfig({kind: "bubble", action: "create", item: {name: ids.bubbleConfigName.value.trim() || "新泡泡流程", values: bubbleConfigValues()}, sync: false});
      } catch (error) {
        setNotice(`新建泡泡配置失败：${error.message || error}`);
      }
    });
    ids.bubbleConfigSave.addEventListener("click", async () => {
      try {
        const id = ids.bubbleConfigSelect.value;
        if (!id) {
          await postConfig({kind: "bubble", action: "create", item: {name: ids.bubbleConfigName.value.trim() || "新泡泡流程", values: bubbleConfigValues()}, sync: false});
        } else {
          await postConfig({kind: "bubble", action: "save", id, item: {id, name: ids.bubbleConfigName.value.trim() || "未命名泡泡流程", values: bubbleConfigValues()}, sync: false});
        }
      } catch (error) {
        setNotice(`泡泡配置保存失败：${error.message || error}`);
      }
    });
    ids.bubbleConfigRename.addEventListener("click", async () => {
      try {
        await postConfig({kind: "bubble", action: "rename", id: ids.bubbleConfigSelect.value, name: ids.bubbleConfigName.value.trim(), sync: false});
      } catch (error) {
        setNotice(`泡泡配置重命名失败：${error.message || error}`);
      }
    });
    ids.bubbleConfigDelete.addEventListener("click", async () => {
      try {
        await postConfig({kind: "bubble", action: "delete", id: ids.bubbleConfigSelect.value, sync: false});
      } catch (error) {
        setNotice(`泡泡配置删除失败：${error.message || error}`);
      }
    });
    ids.testEegLightColor.addEventListener("click", async () => {
      try {
        const response = await fetch("/api/eeg-light-test");
        const data = await response.json();
        if (!response.ok || !data.ok) throw new Error(data.error || response.status);
        ids.eegTestSwatch.style.background = data.color.hex;
        setNotice(`测试色：${data.color.hex}，attention ${data.color.attention} / meditation ${data.color.meditation}`);
      } catch (error) {
        setNotice(`不可测试脑电灯光：${error.message || error}`);
      }
    });
    ids.clearLog.addEventListener("click", () => {
      suppressLogRender = true;
      ids.log.textContent = "";
      setCommandFeedback("ok", "已清空", 1200);
      setNotice("事件日志显示已清空。");
      setTimeout(() => suppressLogRender = false, 2500);
    });
    ids.stepperTurns.addEventListener("input", updateStepperPreview);
    BUBBLE_CONFIG_INPUT_IDS.forEach((id) => ids[id]?.addEventListener("input", updateBubbleConfigPreview));
    BUBBLE_TIMELINE_INPUT_IDS.forEach((id) => ids[id]?.addEventListener("change", () => {
      ids[id].value = timelineSeconds(id, 0).toFixed(1);
      updateBubbleConfigPreview();
    }));
    updateStepperPreview();
    updateBubbleConfigPreview();
    renderVibrationHistory();

    const update = (state) => {
      const eegSeen = Boolean(state.eeg.seen);
      const m5Seen = Boolean(state.m5.seen);
      const micSeen = Boolean(state.mic.seen && state.m5.micStatus === "YES" && state.m5.micAgeMs >= 0 && state.m5.micAgeMs < 3000);
      const serialReady = Boolean(state.bridge.sourceOpen && state.bridge.targetOpen);
      const commandReady = Boolean(state.bridge.targetOpen);
      const outputReady = Boolean(commandReady && micSeen && state.mic.systemEnabled);
      const bubbleActive = Boolean(micSeen && state.mic.bubbleState > 0);
      const vibrationSeen = Boolean(m5Seen && state.m5.vibrationStatus === "YES" && state.m5.vibrationAgeMs >= 0 && state.m5.vibrationAgeMs < 3000);
      const vibrationEnabled = state.m5.vibrationEnabled !== false;
      const fogOn = Boolean(micSeen && state.mic.relayState === 2);
      const fanOn = Boolean(micSeen && state.mic.fanState === 2);
      const relayOutputEnabled = Boolean(micSeen && state.mic.relayOutputEnabled);
      const storeSignature = JSON.stringify(state.store || {});
      if (state.store && storeSignature !== configStoreSignature && !editingConfigInput()) {
        configStoreSignature = storeSignature;
        renderConfigStore(state.store);
      }
      latestM5Seen = m5Seen;
      latestVibrationEnabled = vibrationEnabled;
      pill(ids.webStatus, "ok", "已连接");
      pill(ids.sourceStatus, serialReady ? "ok" : "warn", serialReady ? "串口在线" : "等待串口");
      pill(ids.m5Status, m5Seen ? "ok" : "warn", m5Seen ? "在线" : "等待");
      pill(ids.micStatus, micSeen ? "ok" : "warn", micSeen ? "在线" : "等待");
      pill(ids.vibrationStatus, vibrationSeen ? "ok" : "warn", vibrationSeen ? "在线" : (m5Seen ? "等待" : "M5等待"));
      pill(ids.eegStatus, eegSeen ? (state.eeg.ageMs < 1200 ? "ok" : "warn") : "warn", eegSeen ? "真实数据" : "等待脑电");
      setText("eegDiagnosis", state.eeg.diagnosis || "--");
      pill(ids.systemEnabled, micSeen ? (state.mic.systemEnabled ? "ok" : "bad") : "neutral", micSeen ? (state.mic.systemEnabled ? "系统已开启" : "系统关闭") : "--");
      setCommandReady(commandReady);
      setControlAvailability(commandReady, micSeen, Boolean(state.mic.systemEnabled), bubbleActive, relayOutputEnabled, fogOn, fanOn);

      setText("sourcePort", `${state.config.source}@${state.config.sourceBaud}`);
      setText("targetPort", `${state.config.target}@${state.config.targetBaud}`);
      setText("sendRate", `${state.config.sendRate} Hz`);
      setText("lastError", state.bridge.lastError || "--");
      setText("eegAge", eegSeen ? `${state.eeg.ageMs} ms` : "-- ms");
      setText("m5Age", m5Seen ? `${state.m5.ageMs} ms` : "-- ms");
      setText("micAge", micSeen ? `${state.mic.ageMs} ms` : "-- ms");

      setText("poorSignal", realText(eegSeen, state.eeg.poorSignal));
      setText("attention", realText(eegSeen, state.eeg.attention));
      setText("meditation", realText(eegSeen, state.eeg.meditation));
      setBar("poorSignalBar", eegSeen, state.eeg.poorSignal, 200, true);
      setBar("attentionBar", eegSeen, state.eeg.attention);
      setBar("meditationBar", eegSeen, state.eeg.meditation);
      EEG_POWER_FIELDS.forEach((name, index) => setText(name, realText(eegSeen, state.eeg.eegPower[index])));
      setText("validPacketsPill", eegSeen ? `${state.stats.validPackets} 包` : "-- 包");

      setText("lightMode", modeText(micSeen, lightModes, state.mic.lightMode));
      setText("lightLevel", micSeen ? `level ${state.mic.lightLevel} L1 ${rgbwText(state.mic.light1Rgbw)} L2 ${rgbwText(state.mic.light2Rgbw)}` : "level --");
      setText("relayState", micSeen ? `雾 ${modeText(true, relayStates, state.mic.relayState)} / 风 ${modeText(true, relayStates, state.mic.fanState)}` : "--");
      setText("lightCurrentState", onOffText(micSeen, state.mic.lightLevel > 0, modeText(micSeen, lightModes, state.mic.lightMode)));
      setLightModeAvailability(state, commandReady, micSeen);
      setText("fogCurrentState", onOffText(micSeen, fogOn, modeText(micSeen, relayStates, state.mic.relayState)));
      setText("fanCurrentState", onOffText(micSeen, fanOn, modeText(micSeen, relayStates, state.mic.fanState)));
      setText("stepperState", modeText(micSeen, stepperStates, state.mic.stepperState));
      setText("safetyState", modeText(micSeen, safetyStates, state.mic.safetyState));
      setText("lastAction", actionText(micSeen, state.mic.lastControlAction));
      setText("relayEnabled", micSeen ? (state.mic.relayOutputEnabled ? "雾机/风扇输出已启用" : "固件输出关闭") : "--");
      setText("stepperEnabled", micSeen ? (state.mic.stepperOutputEnabled ? "固件输出已启用" : "固件输出关闭") : "--");
      setText("vibrationState", vibrationSeen ? (state.m5.vibrationDetected ? "震动中" : "静止") : "--");
      setText("vibrationDetail", vibrationSeen ? `value ${state.m5.vibrationSensor} / count ${state.m5.vibrationTriggerCount}` : "ESP32-S3 等待");
      setText("vibrationCount", vibrationSeen ? state.m5.vibrationTriggerCount : "--");
      setText("vibrationLast", vibrationSeen && state.m5.vibrationLastMs >= 0 ? `${Math.round(state.m5.vibrationLastMs / 1000)} 秒` : "--");
      setText("vibrationMode", m5Seen ? (vibrationEnabled ? "已启用" : "已停用") : "--");
      updateVibrationHistory(state, vibrationSeen);
      pill(ids.vibrationSource, vibrationSeen ? (!vibrationEnabled ? "neutral" : (state.m5.vibrationDetected ? "warn" : "ok")) : "neutral", vibrationSeen ? (!vibrationEnabled ? "震动停用" : "震动板在线") : (m5Seen ? "等待震动板" : "M5 等待"));
      setText("bubbleState", modeText(micSeen, bubbleStates, state.mic.bubbleState));
      setText("bubbleDetail", micSeen ? `count ${state.mic.bubbleTriggerCount} / ${Math.round(state.mic.bubbleActiveMs / 1000)} 秒` : "--");
      pill(
        ids.relayHint,
        micSeen ? (!state.mic.systemEnabled ? "neutral" : (!relayOutputEnabled ? "warn" : (bubbleActive ? "warn" : ((fogOn || fanOn) ? "ok" : "neutral")))) : "neutral",
        micSeen ? (!state.mic.systemEnabled ? "需系统开启" : (!relayOutputEnabled ? "输出未启用" : (bubbleActive ? "泡泡流程中" : ((fogOn || fanOn) ? "有输出" : "待机")))) : "--"
      );
      pill(ids.stepperHint, micSeen ? (!state.mic.systemEnabled ? "neutral" : (state.mic.stepperOutputEnabled ? "ok" : "warn")) : "neutral", micSeen ? (!state.mic.systemEnabled ? "需系统开启" : (bubbleActive ? "泡泡流程中" : (state.mic.stepperOutputEnabled ? "可输出" : "输出关闭"))) : "--");

      setText("sentFrames", realText(state.bridge.targetOpen, state.stats.sentFrames));
      setText("validPackets", realText(eegSeen, state.stats.validPackets));
      setText("checksumErrors", eegSeen ? `checksum ${state.stats.checksumErrors}` : "checksum --");
      setText("espNowStats", m5Seen ? `tx ${state.m5.espNowTx} / fail ${state.m5.espNowFail}` : "--");
      setText("m5Command", m5Seen ? `cmd rx ${state.m5.cmdRx} / tx ${state.m5.cmdTx}` : "cmd --");
      setText("micPackets", micSeen ? `rx ${state.mic.rxCount} / drop ${state.mic.dropCount}` : "--");
      setText("micControl", micSeen ? `control ${state.mic.controlRxCount}` : "control --");
      if (!localNotice) {
        if (!commandReady) {
          ids.notice.textContent = "等待 M5Stack 串口打开。";
        } else if (!micSeen) {
          ids.notice.textContent = "等待 Microduino 状态；可先使用系统开启命令。";
        } else if (!state.mic.systemEnabled) {
          ids.notice.textContent = "系统关闭：灯光、泡泡、雾机、风扇和步进电机需先开启系统。";
        } else {
          ids.notice.textContent = "系统已开启：输出控制会通过 M5Stack 串口转发。";
        }
      }
      renderLogs(state.bridge.logs);
    };

    const events = new EventSource("/events");
    events.onmessage = (event) => update(JSON.parse(event.data));
    events.onerror = () => {
      pill(ids.webStatus, "bad", "断开");
      pill(ids.vibrationStatus, "warn", "等待");
      setCommandReady(false);
      setControlAvailability(false, false, false, false);
      setNotice("前端与本地服务断开。");
    };
  </script>
</body>
</html>
"""


@dataclass
class DreamEegFrame:
    poor_signal: int = DEFAULT_POOR_SIGNAL
    attention: int = 0
    meditation: int = 0
    eeg_power: list[int] = field(default_factory=lambda: [0] * 8)
    seen: bool = False
    source_packet_count: int = 0
    last_source_time: float = 0.0


@dataclass
class BridgeStats:
    raw_bytes: int = 0
    valid_packets: int = 0
    checksum_errors: int = 0
    length_errors: int = 0
    parse_errors: int = 0
    sent_frames: int = 0
    command_frames: int = 0
    target_lines: int = 0


@dataclass
class M5Status:
    serial_frames: int = 0
    serial_errors: int = 0
    espnow_tx: int = 0
    espnow_fail: int = 0
    cmd_rx: int = 0
    cmd_tx: int = 0
    cmd_fail: int = 0
    mic_status: str = "NO"
    serial_age_ms: int = 0
    mic_age_ms: int = 0
    vibration_status: str = "NO"
    vibration_age_ms: int = 0
    vibration_detected: int = 0
    vibration_sensor: int = 0
    vibration_bubble_state: int = 0
    vibration_trigger_count: int = 0
    vibration_last_ms: int = -1
    vibration_enabled: int = 1
    pressure_pressed: int = 0
    pressure_trigger_count: int = 0
    pressure_last_ms: int = -1
    seen: bool = False
    last_update_time: float = 0.0


@dataclass
class MicStatus:
    rx_count: int = 0
    drop_count: int = 0
    timeout_count: int = 0
    light_mode: int = 0
    light_level: int = 0
    light1_rgbw: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    light2_rgbw: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    stepper_state: int = 0
    relay_state: int = 0
    fan_state: int = 0
    bubble_state: int = 0
    bubble_trigger_count: int = 0
    bubble_active_ms: int = 0
    safety_state: int = 2
    control_rx_count: int = 0
    last_control_action: int = 0
    manual_light_enabled: int = 0
    relay_output_enabled: int = 0
    stepper_output_enabled: int = 0
    bubble_output_enabled: int = 0
    runtime_flags: int = 0
    system_enabled: int = 0
    seen: bool = False
    last_update_time: float = 0.0


@dataclass
class RuntimeState:
    source_open: bool = False
    target_open: bool = False
    last_error: str = ""
    last_command: str = ""
    recent_logs: list[str] = field(default_factory=list)


def clamp_number(value: object, fallback: float, minimum: float, maximum: float) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        number = fallback
    return max(minimum, min(maximum, number))


def clamp_int(value: object, fallback: int, minimum: int, maximum: int) -> int:
    return int(round(clamp_number(value, fallback, minimum, maximum)))


def normalize_hex_color(value: object, fallback: str = "#4b7dff") -> str:
    text = str(value or fallback).strip()
    if not text.startswith("#"):
        text = f"#{text}"
    if len(text) != 7:
        return fallback
    try:
        int(text[1:], 16)
    except ValueError:
        return fallback
    return text.lower()


def hex_to_rgb(value: str) -> tuple[int, int, int]:
    text = normalize_hex_color(value)
    return int(text[1:3], 16), int(text[3:5], 16), int(text[5:7], 16)


def seconds_to_ms(value: object) -> int:
    return clamp_int(clamp_number(value, 0.0, 0.0, 60.0) * 1000, 0, 0, 60000)


def sample_palette_color(palette: dict, position: int) -> dict:
    stops = palette.get("stops") if isinstance(palette.get("stops"), list) else default_palette()["stops"]
    clean = sorted(
        [
            {
                "position": clamp_int(stop.get("position"), 0, 0, 100),
                "color": normalize_hex_color(stop.get("color")),
                "white": clamp_int(stop.get("white"), 0, 0, 255),
            }
            for stop in stops if isinstance(stop, dict)
        ],
        key=lambda stop: stop["position"],
    )
    if not clean:
        clean = default_palette()["stops"]
    target = clamp_int(position, 0, 0, 100)
    left = clean[0]
    right = clean[-1]
    for index, stop in enumerate(clean):
        if stop["position"] <= target:
            left = stop
        if stop["position"] >= target:
            right = stop
            break
    if left["position"] == right["position"]:
        red, green, blue = hex_to_rgb(left["color"])
        return {"r": red, "g": green, "b": blue, "w": left["white"], "hex": left["color"]}
    amount = (target - left["position"]) / (right["position"] - left["position"])
    left_rgb = hex_to_rgb(left["color"])
    right_rgb = hex_to_rgb(right["color"])
    mixed = [round(left_rgb[i] * (1 - amount) + right_rgb[i] * amount) for i in range(3)]
    white = round(left["white"] * (1 - amount) + right["white"] * amount)
    return {"r": mixed[0], "g": mixed[1], "b": mixed[2], "w": white, "hex": f"#{mixed[0]:02x}{mixed[1]:02x}{mixed[2]:02x}"}


def make_config_id(prefix: str) -> str:
    return f"{prefix}-{int(time.time() * 1000)}"


def default_palette() -> dict:
    return {
        "id": "default-palette",
        "name": "Dream 默认色板",
        "stops": [
            {"position": 0, "color": "#4b7dff", "white": 0},
            {"position": 42, "color": "#20c7bd", "white": 0},
            {"position": 72, "color": "#ff5cc8", "white": 8},
            {"position": 100, "color": "#ff8a4c", "white": 10},
        ],
        "twoLightOffsetSec": 1.0,
        "flowPeriodSec": 6.0,
    }


class DreamConfigStore:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = self.load()

    def default_data(self) -> dict:
        palette = default_palette()
        return {
            "app": {
                "runMode": "with_eeg",
                "lightStrategy": "auto",
                "selectedPaletteId": palette["id"],
                "selectedBubbleConfigId": "",
            },
            "palettes": [palette],
            "bubbleConfigs": [],
        }

    def load(self) -> dict:
        if not self.path.exists():
            data = self.default_data()
            self.write(data)
            return data
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                data = json.load(handle)
        except Exception:
            data = self.default_data()
            self.write(data)
            return data
        return self.normalize(data)

    def write(self, data: Optional[dict] = None) -> None:
        payload = self.normalize(data or self.data)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
        self.data = payload

    def normalize(self, data: dict) -> dict:
        defaults = self.default_data()
        app = data.get("app") if isinstance(data.get("app"), dict) else {}
        palettes = data.get("palettes") if isinstance(data.get("palettes"), list) else []
        bubble_configs = data.get("bubbleConfigs") if isinstance(data.get("bubbleConfigs"), list) else []
        clean_palettes = [self.sanitize_palette(item) for item in palettes if isinstance(item, dict)]
        if not clean_palettes:
            clean_palettes = defaults["palettes"]
        clean_bubble_configs = [self.sanitize_bubble_config(item) for item in bubble_configs if isinstance(item, dict)]
        palette_ids = {item["id"] for item in clean_palettes}
        bubble_ids = {item["id"] for item in clean_bubble_configs}
        selected_palette = str(app.get("selectedPaletteId") or "")
        if selected_palette not in palette_ids:
            selected_palette = clean_palettes[0]["id"]
        selected_bubble = str(app.get("selectedBubbleConfigId") or "")
        if selected_bubble not in bubble_ids:
            selected_bubble = ""
        run_mode = str(app.get("runMode") or defaults["app"]["runMode"])
        if run_mode not in RUN_MODE_CODES:
            run_mode = defaults["app"]["runMode"]
        light_strategy = str(app.get("lightStrategy") or defaults["app"]["lightStrategy"])
        if light_strategy not in LIGHT_STRATEGY_CODES:
            light_strategy = defaults["app"]["lightStrategy"]
        return {
            "app": {
                "runMode": run_mode,
                "lightStrategy": light_strategy,
                "selectedPaletteId": selected_palette,
                "selectedBubbleConfigId": selected_bubble,
            },
            "palettes": clean_palettes,
            "bubbleConfigs": clean_bubble_configs,
        }

    def sanitize_palette(self, item: dict) -> dict:
        stops = item.get("stops") if isinstance(item.get("stops"), list) else []
        clean_stops = []
        for stop in stops:
            if not isinstance(stop, dict):
                continue
            clean_stops.append({
                "position": clamp_int(stop.get("position"), 0, 0, 100),
                "color": normalize_hex_color(stop.get("color")),
                "white": clamp_int(stop.get("white"), 0, 0, 255),
            })
        if not clean_stops:
            clean_stops = default_palette()["stops"]
        clean_stops.sort(key=lambda stop: stop["position"])
        return {
            "id": str(item.get("id") or make_config_id("palette")),
            "name": str(item.get("name") or "未命名色板")[:60],
            "stops": clean_stops[:12],
            "twoLightOffsetSec": round(clamp_number(item.get("twoLightOffsetSec"), 1.0, 0.0, 60.0), 1),
            "flowPeriodSec": round(clamp_number(item.get("flowPeriodSec"), 6.0, 0.5, 60.0), 1),
        }

    def sanitize_bubble_config(self, item: dict) -> dict:
        values = item.get("values") if isinstance(item.get("values"), dict) else {}
        return {
            "id": str(item.get("id") or make_config_id("bubble")),
            "name": str(item.get("name") or "未命名泡泡流程")[:60],
            "values": {
                "reverseTurns": round(clamp_number(values.get("reverseTurns"), 1.0, 0.0, 20.0), 1),
                "forwardTurns": round(clamp_number(values.get("forwardTurns"), 1.0, 0.0, 20.0), 1),
                "fogStartSec": round(clamp_number(values.get("fogStartSec"), 1.0, 0.0, 60.0), 1),
                "fanLightStartSec": round(clamp_number(values.get("fanLightStartSec"), 5.0, 0.0, 60.0), 1),
                "forwardStartSec": round(clamp_number(values.get("forwardStartSec"), 9.0, 0.0, 60.0), 1),
                "fogStopSec": round(clamp_number(values.get("fogStopSec"), 12.7, 0.0, 60.0), 1),
                "fanStopSec": round(clamp_number(values.get("fanStopSec"), 13.7, 0.0, 60.0), 1),
                "lightStopSec": round(clamp_number(values.get("lightStopSec"), 16.7, 0.0, 60.0), 1),
            },
        }

    def snapshot(self) -> dict:
        return copy.deepcopy(self.data)

    def selected_palette(self) -> dict:
        selected_id = self.data["app"]["selectedPaletteId"]
        for palette in self.data["palettes"]:
            if palette["id"] == selected_id:
                return palette
        return self.data["palettes"][0]

    def apply_operation(self, payload: dict) -> dict:
        kind = str(payload.get("kind") or "")
        action = str(payload.get("action") or "")
        if kind == "app" and action == "update":
            app = dict(self.data["app"])
            updates = payload.get("app") if isinstance(payload.get("app"), dict) else {}
            if updates.get("runMode") in RUN_MODE_CODES:
                app["runMode"] = updates["runMode"]
            if updates.get("lightStrategy") in LIGHT_STRATEGY_CODES:
                app["lightStrategy"] = updates["lightStrategy"]
            palette_id = updates.get("selectedPaletteId")
            if palette_id and any(item["id"] == palette_id for item in self.data["palettes"]):
                app["selectedPaletteId"] = palette_id
            bubble_id = updates.get("selectedBubbleConfigId")
            if bubble_id == "" or any(item["id"] == bubble_id for item in self.data["bubbleConfigs"]):
                app["selectedBubbleConfigId"] = bubble_id
            self.data["app"] = app
            self.write()
            return self.snapshot()

        if kind == "palette":
            return self.apply_palette_operation(action, payload)
        if kind == "bubble":
            return self.apply_bubble_operation(action, payload)
        raise ValueError(f"unknown store operation: {kind}/{action}")

    def apply_palette_operation(self, action: str, payload: dict) -> dict:
        item = payload.get("item") if isinstance(payload.get("item"), dict) else {}
        item_id = str(payload.get("id") or item.get("id") or "")
        if action == "create":
            palette = self.sanitize_palette({**item, "id": make_config_id("palette")})
            self.data["palettes"].append(palette)
            self.data["app"]["selectedPaletteId"] = palette["id"]
        elif action == "save":
            palette = self.sanitize_palette({**item, "id": item_id})
            for index, existing in enumerate(self.data["palettes"]):
                if existing["id"] == item_id:
                    self.data["palettes"][index] = palette
                    break
            else:
                raise ValueError("palette not found")
        elif action == "rename":
            name = str(payload.get("name") or item.get("name") or "")[:60]
            if not name:
                raise ValueError("palette name is empty")
            for palette in self.data["palettes"]:
                if palette["id"] == item_id:
                    palette["name"] = name
                    break
            else:
                raise ValueError("palette not found")
        elif action == "delete":
            if len(self.data["palettes"]) <= 1:
                raise ValueError("at least one palette is required")
            self.data["palettes"] = [palette for palette in self.data["palettes"] if palette["id"] != item_id]
            if self.data["app"]["selectedPaletteId"] == item_id:
                self.data["app"]["selectedPaletteId"] = self.data["palettes"][0]["id"]
        elif action == "select":
            if not any(palette["id"] == item_id for palette in self.data["palettes"]):
                raise ValueError("palette not found")
            self.data["app"]["selectedPaletteId"] = item_id
        else:
            raise ValueError(f"unknown palette action: {action}")
        self.write()
        return self.snapshot()

    def apply_bubble_operation(self, action: str, payload: dict) -> dict:
        item = payload.get("item") if isinstance(payload.get("item"), dict) else {}
        item_id = str(payload.get("id") or item.get("id") or "")
        if action == "create":
            config = self.sanitize_bubble_config({**item, "id": make_config_id("bubble")})
            self.data["bubbleConfigs"].append(config)
            self.data["app"]["selectedBubbleConfigId"] = config["id"]
        elif action == "save":
            config = self.sanitize_bubble_config({**item, "id": item_id})
            for index, existing in enumerate(self.data["bubbleConfigs"]):
                if existing["id"] == item_id:
                    self.data["bubbleConfigs"][index] = config
                    break
            else:
                raise ValueError("bubble config not found")
        elif action == "rename":
            name = str(payload.get("name") or item.get("name") or "")[:60]
            if not name:
                raise ValueError("bubble config name is empty")
            for config in self.data["bubbleConfigs"]:
                if config["id"] == item_id:
                    config["name"] = name
                    break
            else:
                raise ValueError("bubble config not found")
        elif action == "delete":
            self.data["bubbleConfigs"] = [config for config in self.data["bubbleConfigs"] if config["id"] != item_id]
            if self.data["app"]["selectedBubbleConfigId"] == item_id:
                self.data["app"]["selectedBubbleConfigId"] = ""
        elif action == "select":
            if item_id and not any(config["id"] == item_id for config in self.data["bubbleConfigs"]):
                raise ValueError("bubble config not found")
            self.data["app"]["selectedBubbleConfigId"] = item_id
        else:
            raise ValueError(f"unknown bubble action: {action}")
        self.write()
        return self.snapshot()


class ThinkGearParser:
    WAIT_SYNC_1 = 0
    WAIT_SYNC_2 = 1
    READ_LENGTH = 2
    READ_PAYLOAD = 3
    READ_CHECKSUM = 4

    def __init__(self) -> None:
        self.state = self.WAIT_SYNC_1
        self.payload_length = 0
        self.payload = bytearray()
        self.payload_sum = 0

    def feed(self, value: int, eeg_frame: DreamEegFrame, stats: BridgeStats) -> bool:
        if self.state == self.WAIT_SYNC_1:
            if value == 0xAA:
                self.state = self.WAIT_SYNC_2
            return False
        if self.state == self.WAIT_SYNC_2:
            self.state = self.READ_LENGTH if value == 0xAA else self.WAIT_SYNC_1
            return False
        if self.state == self.READ_LENGTH:
            if value == 0 or value > THINKGEAR_MAX_PAYLOAD:
                stats.length_errors += 1
                self.state = self.WAIT_SYNC_2 if value == 0xAA else self.WAIT_SYNC_1
                return False
            self.payload_length = value
            self.payload = bytearray()
            self.payload_sum = 0
            self.state = self.READ_PAYLOAD
            return False
        if self.state == self.READ_PAYLOAD:
            self.payload.append(value)
            self.payload_sum = (self.payload_sum + value) & 0xFF
            if len(self.payload) >= self.payload_length:
                self.state = self.READ_CHECKSUM
            return False
        if self.state == self.READ_CHECKSUM:
            expected = (~self.payload_sum) & 0xFF
            self.state = self.WAIT_SYNC_1
            if value != expected:
                stats.checksum_errors += 1
                return False
            if parse_thinkgear_payload(self.payload, eeg_frame):
                stats.valid_packets += 1
                eeg_frame.source_packet_count += 1
                eeg_frame.last_source_time = time.monotonic()
                eeg_frame.seen = True
                return True
            stats.parse_errors += 1
            return False
        self.state = self.WAIT_SYNC_1
        return False


class DreamBridge:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.parser = ThinkGearParser()
        self.eeg_frame = DreamEegFrame()
        self.stats = BridgeStats()
        self.m5_status = M5Status()
        self.mic_status = MicStatus()
        self.runtime = RuntimeState()
        self.store = DreamConfigStore(Path(__file__).with_name(CONFIG_STORE_NAME))
        self.lock = threading.Lock()
        self.command_queue: queue.Queue[str] = queue.Queue()
        self.start_time = time.monotonic()
        self.seq = 0
        self.command_seq = 0
        self.stop_event = threading.Event()
        self.log_file = None
        self.log_writer = None

    def log(self, message: str) -> None:
        print(message)
        with self.lock:
            self.runtime.recent_logs.insert(0, message)
            del self.runtime.recent_logs[80:]

    def open_log_writer(self) -> None:
        if not self.args.log_file:
            return
        path = Path(self.args.log_file)
        path.parent.mkdir(parents=True, exist_ok=True)
        self.log_file = path.open("w", newline="", encoding="utf-8")
        self.log_writer = csv.writer(self.log_file)
        self.log_writer.writerow(["timeMs", "seq", "poorSignal", "attention", "meditation", *EEG_POWER_FIELDS])

    def close_log_writer(self) -> None:
        if self.log_file is not None:
            self.log_file.close()

    def eeg_usable_for_control(self) -> tuple[bool, str]:
        now = time.monotonic()
        with self.lock:
            if not self.eeg_frame.seen:
                return False, "尚未收到 ThinkGear 有效数据包"
            age_ms = int((now - self.eeg_frame.last_source_time) * 1000)
            if age_ms > EEG_CONTROL_STALE_MS:
                return False, f"最近脑电数据已过期：{age_ms} ms"
            if self.eeg_frame.poor_signal > EEG_CONTROL_POOR_SIGNAL_MAX:
                return False, f"poorSignal={self.eeg_frame.poor_signal}，信号不可用于灯光"
            if self.eeg_frame.attention <= 0 and self.eeg_frame.meditation <= 0:
                return False, "attention/meditation 仍为空"
            return True, "脑电信号可用于灯光"

    def eeg_palette_color(self, palette: Optional[dict] = None) -> dict:
        palette = palette or self.store.selected_palette()
        with self.lock:
            attention = self.eeg_frame.attention
            meditation = self.eeg_frame.meditation
        position = max(0, min(100, round((attention * 0.68 + meditation * 0.32))))
        color = sample_palette_color(palette, position)
        color["position"] = position
        color["attention"] = attention
        color["meditation"] = meditation
        return color

    def sync_runtime_to_board(self) -> list[str]:
        warnings: list[str] = []
        store = self.store.snapshot()
        app = store["app"]
        palette = self.store.selected_palette()
        commands: list[tuple[str, list[int]]] = [
            ("run_mode", [RUN_MODE_CODES[app["runMode"]], 0, 0, 0]),
            (
                "light_strategy",
                [
                    LIGHT_STRATEGY_CODES[app["lightStrategy"]],
                    seconds_to_ms(palette["flowPeriodSec"]),
                    seconds_to_ms(palette["twoLightOffsetSec"]),
                    0,
                ],
            ),
            (
                "palette_settings",
                [
                    seconds_to_ms(palette["twoLightOffsetSec"]),
                    seconds_to_ms(palette["flowPeriodSec"]),
                    min(len(palette["stops"]), 6),
                    0,
                ],
            ),
        ]
        for index, stop in enumerate(palette["stops"][:6]):
            red, green, blue = hex_to_rgb(stop["color"])
            commands.append((
                "palette_node",
                [
                    index,
                    min(len(palette["stops"]), 6),
                    clamp_int(stop["position"] * 10, 0, 0, 1000),
                    red,
                    green,
                    blue,
                    clamp_int(stop.get("white"), 0, 0, 255),
                    0,
                ],
            ))
        for action, args in commands:
            try:
                self.queue_command(action, args)
            except Exception as exc:
                warnings.append(str(exc))
                break
        if warnings:
            self.log(f"EVENT=RUNTIME_SYNC_DEFERRED REASON={warnings[0]}")
        else:
            self.log(
                "EVENT=RUNTIME_SYNC_QUEUED "
                f"RUN_MODE={app['runMode']} LIGHT_STRATEGY={app['lightStrategy']} "
                f"PALETTE={palette['name']}"
            )
        return warnings

    def apply_store_operation(self, payload: dict) -> dict:
        store = self.store.apply_operation(payload)
        warnings: list[str] = []
        sync_requested = bool(payload.get("sync", True))
        kind = payload.get("kind")
        if sync_requested and kind in {"app", "palette"}:
            warnings = self.sync_runtime_to_board()
        return {"ok": True, "store": store, "warnings": warnings}

    def run_serial_loop(self) -> None:
        send_interval = 1.0 / self.args.send_rate
        next_send_time = time.monotonic() + send_interval
        next_print_time = time.monotonic() + self.args.print_rate
        next_source_retry_time = 0.0
        next_target_retry_time = 0.0
        source: Optional[serial.Serial] = None
        target: Optional[serial.Serial] = None
        self.open_log_writer()
        try:
            self.log(
                "EVENT=DREAM_BOOT ROLE=pcBridge "
                f"VERSION={PC_BRIDGE_VERSION} SOURCE={self.args.source}@{self.args.source_baud} "
                f"TARGET={self.args.target}@{self.args.target_baud} SEND_RATE={self.args.send_rate} "
                f"WEB=http://127.0.0.1:{self.args.web_port}/"
            )

            while not self.stop_event.is_set():
                now = time.monotonic()

                if target is None and now >= next_target_retry_time:
                    try:
                        target = open_serial_port(self.args.target, self.args.target_baud)
                        target.reset_input_buffer()
                        with self.lock:
                            self.runtime.target_open = True
                            self.runtime.last_error = ""
                        self.log(f"EVENT=TARGET_SERIAL_OPEN PORT={self.args.target} BAUD={self.args.target_baud}")
                    except Exception as exc:
                        next_target_retry_time = now + SERIAL_RECONNECT_INTERVAL_S
                        with self.lock:
                            self.runtime.target_open = False
                            self.runtime.last_error = str(exc)
                        self.log(f"EVENT=TARGET_SERIAL_OPEN_FAIL PORT={self.args.target} ERROR={exc}")

                if self.args.source and source is None and now >= next_source_retry_time:
                    try:
                        source = open_serial_port(self.args.source, self.args.source_baud)
                        with self.lock:
                            self.runtime.source_open = True
                            self.runtime.last_error = ""
                        self.log(f"EVENT=SOURCE_SERIAL_OPEN PORT={self.args.source} BAUD={self.args.source_baud}")
                    except Exception as exc:
                        next_source_retry_time = now + SERIAL_RECONNECT_INTERVAL_S
                        with self.lock:
                            self.runtime.source_open = False
                            self.runtime.last_error = str(exc)
                        self.log(f"EVENT=SOURCE_SERIAL_OPEN_FAIL PORT={self.args.source} ERROR={exc}")

                if source is not None:
                    try:
                        raw = source.read(max(1, self.args.read_chunk))
                    except Exception as exc:
                        self.log(f"EVENT=SOURCE_SERIAL_LOST PORT={self.args.source} ERROR={exc}")
                        source.close()
                        source = None
                        next_source_retry_time = time.monotonic() + SERIAL_RECONNECT_INTERVAL_S
                        with self.lock:
                            self.runtime.source_open = False
                            self.runtime.last_error = str(exc)
                        raw = b""

                    if raw:
                        with self.lock:
                            self.stats.raw_bytes += len(raw)
                        for value in raw:
                            with self.lock:
                                self.parser.feed(value, self.eeg_frame, self.stats)

                now = time.monotonic()
                with self.lock:
                    eeg_age_ms = int((now - self.eeg_frame.last_source_time) * 1000) if self.eeg_frame.seen else -1
                    should_send = (
                        target is not None
                        and self.eeg_frame.seen
                        and eeg_age_ms <= EEG_SEND_STALE_MS
                        and now >= next_send_time
                    )
                if should_send:
                    try:
                        self.send_eeg_frame(target, now)
                    except Exception as exc:
                        self.log(f"EVENT=TARGET_SERIAL_LOST PORT={self.args.target} ERROR={exc}")
                        target.close()
                        target = None
                        next_target_retry_time = time.monotonic() + SERIAL_RECONNECT_INTERVAL_S
                        with self.lock:
                            self.runtime.target_open = False
                            self.runtime.last_error = str(exc)
                    next_send_time += send_interval
                    if now - next_send_time > send_interval:
                        next_send_time = now + send_interval
                elif now >= next_send_time:
                    next_send_time = now + send_interval

                if target is not None:
                    try:
                        self.send_pending_commands(target)
                        self.drain_target_log(target)
                    except Exception as exc:
                        self.log(f"EVENT=TARGET_SERIAL_LOST PORT={self.args.target} ERROR={exc}")
                        target.close()
                        target = None
                        next_target_retry_time = time.monotonic() + SERIAL_RECONNECT_INTERVAL_S
                        with self.lock:
                            self.runtime.target_open = False
                            self.runtime.last_error = str(exc)

                now = time.monotonic()
                if now >= next_print_time:
                    self.print_status()
                    next_print_time = now + self.args.print_rate
                    if self.log_file is not None:
                        self.log_file.flush()

                time.sleep(0.005)
        except Exception as exc:
            with self.lock:
                self.runtime.last_error = str(exc)
                self.runtime.source_open = False
                self.runtime.target_open = False
            self.log(f"EVENT=PC_BRIDGE_ERROR ERROR={exc}")
            raise
        finally:
            if source is not None:
                source.close()
            if target is not None:
                target.close()
            with self.lock:
                self.runtime.source_open = False
                self.runtime.target_open = False
            self.close_log_writer()

    def send_eeg_frame(self, target: serial.Serial, now: float) -> None:
        with self.lock:
            time_ms = int((now - self.start_time) * 1000)
            line = build_eeg_csv(self.seq, time_ms, self.eeg_frame)
            log_row = [
                time_ms,
                self.seq,
                self.eeg_frame.poor_signal,
                self.eeg_frame.attention,
                self.eeg_frame.meditation,
                *self.eeg_frame.eeg_power,
            ]
            self.seq += 1
            self.stats.sent_frames += 1
        target.write(line.encode("ascii"))
        if self.log_writer is not None:
            self.log_writer.writerow(log_row)

    def send_pending_commands(self, target: serial.Serial) -> None:
        while True:
            try:
                line = self.command_queue.get_nowait()
            except queue.Empty:
                return
            try:
                target.write(line.encode("ascii"))
            except Exception:
                self.command_queue.put(line)
                raise
            with self.lock:
                self.stats.command_frames += 1
                self.runtime.last_command = line.strip()
            self.log(f"EVENT=CONTROL_QUEUED FRAME={line.strip()}")

    def drain_target_log(self, target: serial.Serial) -> None:
        while target.in_waiting:
            line = target.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            with self.lock:
                self.stats.target_lines += 1
            self.parse_target_line(line)

    def parse_target_line(self, line: str) -> None:
        if line.startswith("EVENT=M5_STATUS "):
            pairs = parse_key_values(line)
            now = time.monotonic()
            with self.lock:
                self.m5_status.seen = True
                self.m5_status.last_update_time = now
                self.m5_status.serial_frames = int_value(pairs, "SERIAL_FRAMES")
                self.m5_status.serial_errors = int_value(pairs, "SERIAL_ERRORS")
                self.m5_status.espnow_tx = int_value(pairs, "ESPNOW_TX")
                self.m5_status.espnow_fail = int_value(pairs, "ESPNOW_FAIL")
                self.m5_status.cmd_rx = int_value(pairs, "CMD_RX")
                self.m5_status.cmd_tx = int_value(pairs, "CMD_TX")
                self.m5_status.cmd_fail = int_value(pairs, "CMD_FAIL")
                self.m5_status.mic_status = pairs.get("MIC_STATUS", "NO")
                self.m5_status.serial_age_ms = int_value(pairs, "SERIAL_AGE_MS")
                self.m5_status.mic_age_ms = int_value(pairs, "MIC_AGE_MS")
                self.m5_status.vibration_status = pairs.get("VIB_STATUS", "NO")
                self.m5_status.vibration_age_ms = int_value(pairs, "VIB_AGE_MS", -1)
                self.m5_status.vibration_detected = int_value(pairs, "VIB_DETECTED")
                self.m5_status.vibration_sensor = int_value(pairs, "VIB_SENSOR")
                self.m5_status.vibration_bubble_state = int_value(pairs, "VIB_BUBBLE")
                self.m5_status.vibration_trigger_count = int_value(pairs, "VIB_TRIGGER_COUNT")
                vibration_last_ms = int_value(pairs, "VIB_LAST_MS", -1)
                self.m5_status.vibration_last_ms = -1 if vibration_last_ms >= 0x7FFFFFFF else vibration_last_ms
                self.m5_status.vibration_enabled = int_value(pairs, "VIB_ENABLED", 1)
                self.m5_status.pressure_pressed = int_value(pairs, "PRESSURE")
                pressure_last_ms = int_value(pairs, "PRESSURE_LAST_MS", -1)
                self.m5_status.pressure_last_ms = -1 if pressure_last_ms >= 0x7FFFFFFF else pressure_last_ms
                self.m5_status.pressure_trigger_count = int_value(pairs, "PRESSURE_TRIGGER_COUNT")
                mic_fresh = self.m5_status.mic_status == "YES" and self.m5_status.mic_age_ms < MIC_STATUS_STALE_MS
                if mic_fresh:
                    self.mic_status.seen = True
                    self.mic_status.last_update_time = now
                    self.mic_status.rx_count = int_value(pairs, "MIC_RX")
                    self.mic_status.drop_count = int_value(pairs, "MIC_DROP")
                    self.mic_status.timeout_count = int_value(pairs, "MIC_TIMEOUT")
                    self.mic_status.light_mode = int_value(pairs, "MIC_LIGHT_MODE")
                    self.mic_status.light_level = int_value(pairs, "MIC_LIGHT_LEVEL")
                    self.mic_status.light1_rgbw = [
                        int_value(pairs, "MIC_L1_R"),
                        int_value(pairs, "MIC_L1_G"),
                        int_value(pairs, "MIC_L1_B"),
                        int_value(pairs, "MIC_L1_W"),
                    ]
                    self.mic_status.light2_rgbw = [
                        int_value(pairs, "MIC_L2_R"),
                        int_value(pairs, "MIC_L2_G"),
                        int_value(pairs, "MIC_L2_B"),
                        int_value(pairs, "MIC_L2_W"),
                    ]
                    self.mic_status.stepper_state = int_value(pairs, "MIC_STEPPER")
                    self.mic_status.relay_state = int_value(pairs, "MIC_RELAY")
                    self.mic_status.fan_state = int_value(pairs, "MIC_FAN")
                    self.mic_status.bubble_state = int_value(pairs, "MIC_BUBBLE")
                    self.mic_status.bubble_trigger_count = int_value(pairs, "MIC_BUBBLE_COUNT")
                    self.mic_status.bubble_active_ms = int_value(pairs, "MIC_BUBBLE_ACTIVE_MS")
                    self.mic_status.safety_state = int_value(pairs, "MIC_SAFETY", 2)
                    self.mic_status.control_rx_count = int_value(pairs, "MIC_CONTROL_RX")
                    self.mic_status.last_control_action = int_value(pairs, "MIC_LAST_ACTION")
                    self.mic_status.manual_light_enabled = int_value(pairs, "MIC_MANUAL_LIGHT")
                    self.mic_status.relay_output_enabled = int_value(pairs, "MIC_RELAY_ENABLED")
                    self.mic_status.stepper_output_enabled = int_value(pairs, "MIC_STEPPER_ENABLED")
                    self.mic_status.bubble_output_enabled = int_value(pairs, "MIC_BUBBLE_ENABLED")
                    self.mic_status.runtime_flags = int_value(pairs, "MIC_RUNTIME")
                    self.mic_status.system_enabled = int_value(pairs, "MIC_SYSTEM_ENABLED")
                else:
                    self.mic_status.seen = False
        if (
            line.startswith("EVENT=CONTROL")
            or line.startswith("EVENT=BUBBLE")
            or line.startswith("EVENT=PRESSURE")
            or line.startswith("EVENT=VIBRATION")
            or line.startswith("EVENT=SERIAL_PARSE_FAIL")
        ):
            self.log(f"M5: {line}")

    def print_status(self) -> None:
        with self.lock:
            age_ms = int((time.monotonic() - self.eeg_frame.last_source_time) * 1000) if self.eeg_frame.seen else -1
            uptime_ms = int((time.monotonic() - self.start_time) * 1000)
            message = (
                "EVENT=PC_BRIDGE_STATUS "
                f"UPTIME_MS={uptime_ms} RAW_BYTES={self.stats.raw_bytes} VALID={self.stats.valid_packets} "
                f"SENT={self.stats.sent_frames} COMMANDS={self.stats.command_frames} "
                f"CHECKSUM_ERR={self.stats.checksum_errors} LENGTH_ERR={self.stats.length_errors} "
                f"PARSE_ERR={self.stats.parse_errors} AGE_MS={age_ms} POOR_SIGNAL={self.eeg_frame.poor_signal} "
                f"ATTENTION={self.eeg_frame.attention} MEDITATION={self.eeg_frame.meditation}"
            )
        self.log(message)

    def queue_command(self, action_key: str, args: list[int]) -> str:
        action_name = CONTROL_ACTIONS.get(action_key)
        if action_name is None:
            raise ValueError(f"unknown action: {action_key}")
        clean_args = [max(0, min(65535, int(value))) for value in (args + [0, 0, 0, 0, 0, 0, 0, 0])[:8]]
        with self.lock:
            if not self.runtime.target_open:
                raise RuntimeError("target serial is not open")
            seq = self.command_seq
            self.command_seq += 1
            time_ms = int((time.monotonic() - self.start_time) * 1000)
        line = (
            f"CMD,{seq},{time_ms},{action_name},"
            f"{clean_args[0]},{clean_args[1]},{clean_args[2]},{clean_args[3]},"
            f"{clean_args[4]},{clean_args[5]},{clean_args[6]},{clean_args[7]}\n"
        )
        self.command_queue.put(line)
        return line.strip()

    def snapshot(self) -> dict:
        now = time.monotonic()
        with self.lock:
            eeg_age = int((now - self.eeg_frame.last_source_time) * 1000) if self.eeg_frame.seen else -1
            m5_age = int((now - self.m5_status.last_update_time) * 1000) if self.m5_status.seen else -1
            mic_age = int((now - self.mic_status.last_update_time) * 1000) if self.mic_status.seen else -1
            eeg_seen = self.eeg_frame.seen
            eeg_poor_signal = self.eeg_frame.poor_signal
            eeg_attention = self.eeg_frame.attention
            eeg_meditation = self.eeg_frame.meditation
            eeg_power = list(self.eeg_frame.eeg_power)
        eeg_usable, eeg_diagnosis = self.eeg_usable_for_control()
        with self.lock:
            return {
                "config": {
                    "source": self.args.source,
                    "sourceBaud": self.args.source_baud,
                    "target": self.args.target,
                    "targetBaud": self.args.target_baud,
                    "sendRate": self.args.send_rate,
                },
                "bridge": {
                    "sourceOpen": self.runtime.source_open,
                    "targetOpen": self.runtime.target_open,
                    "lastError": self.runtime.last_error,
                    "lastCommand": self.runtime.last_command,
                    "logs": list(self.runtime.recent_logs[:20]),
                },
                "stats": {
                    "rawBytes": self.stats.raw_bytes,
                    "validPackets": self.stats.valid_packets,
                    "checksumErrors": self.stats.checksum_errors,
                    "lengthErrors": self.stats.length_errors,
                    "parseErrors": self.stats.parse_errors,
                    "sentFrames": self.stats.sent_frames,
                    "commandFrames": self.stats.command_frames,
                    "targetLines": self.stats.target_lines,
                },
                "eeg": {
                    "seen": eeg_seen,
                    "ageMs": eeg_age,
                    "poorSignal": eeg_poor_signal,
                    "attention": eeg_attention,
                    "meditation": eeg_meditation,
                    "eegPower": eeg_power,
                    "usable": eeg_usable,
                    "diagnosis": eeg_diagnosis,
                },
                "store": self.store.snapshot(),
                "m5": {
                    "seen": self.m5_status.seen,
                    "ageMs": m5_age,
                    "serialFrames": self.m5_status.serial_frames,
                    "serialErrors": self.m5_status.serial_errors,
                    "espNowTx": self.m5_status.espnow_tx,
                    "espNowFail": self.m5_status.espnow_fail,
                    "cmdRx": self.m5_status.cmd_rx,
                    "cmdTx": self.m5_status.cmd_tx,
                    "cmdFail": self.m5_status.cmd_fail,
                    "micStatus": self.m5_status.mic_status,
                    "serialAgeMs": self.m5_status.serial_age_ms,
                    "micAgeMs": self.m5_status.mic_age_ms,
                    "vibrationStatus": self.m5_status.vibration_status,
                    "vibrationAgeMs": self.m5_status.vibration_age_ms,
                    "vibrationDetected": bool(self.m5_status.vibration_detected),
                    "vibrationSensor": self.m5_status.vibration_sensor,
                    "vibrationBubbleState": self.m5_status.vibration_bubble_state,
                    "vibrationTriggerCount": self.m5_status.vibration_trigger_count,
                    "vibrationLastMs": self.m5_status.vibration_last_ms,
                    "vibrationEnabled": bool(self.m5_status.vibration_enabled),
                    "pressurePressed": bool(self.m5_status.pressure_pressed),
                    "pressureTriggerCount": self.m5_status.pressure_trigger_count,
                    "pressureLastMs": self.m5_status.pressure_last_ms,
                },
                "mic": {
                    "seen": self.mic_status.seen,
                    "ageMs": mic_age,
                    "rxCount": self.mic_status.rx_count,
                    "dropCount": self.mic_status.drop_count,
                    "timeoutCount": self.mic_status.timeout_count,
                    "lightMode": self.mic_status.light_mode,
                    "lightLevel": self.mic_status.light_level,
                    "light1Rgbw": list(self.mic_status.light1_rgbw),
                    "light2Rgbw": list(self.mic_status.light2_rgbw),
                    "stepperState": self.mic_status.stepper_state,
                    "relayState": self.mic_status.relay_state,
                    "fanState": self.mic_status.fan_state,
                    "bubbleState": self.mic_status.bubble_state,
                    "bubbleTriggerCount": self.mic_status.bubble_trigger_count,
                    "bubbleActiveMs": self.mic_status.bubble_active_ms,
                    "safetyState": self.mic_status.safety_state,
                    "controlRxCount": self.mic_status.control_rx_count,
                    "lastControlAction": self.mic_status.last_control_action,
                    "manualLightEnabled": self.mic_status.manual_light_enabled,
                    "relayOutputEnabled": self.mic_status.relay_output_enabled,
                    "stepperOutputEnabled": self.mic_status.stepper_output_enabled,
                    "bubbleOutputEnabled": self.mic_status.bubble_output_enabled,
                    "runtimeFlags": self.mic_status.runtime_flags,
                    "systemEnabled": self.mic_status.system_enabled,
                },
            }


class DreamRequestHandler(BaseHTTPRequestHandler):
    bridge: DreamBridge

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            self.send_bytes(INDEX_HTML.encode("utf-8"), "text/html; charset=utf-8")
            return
        if path == "/api/state":
            self.send_json(self.bridge.snapshot())
            return
        if path == "/api/config":
            self.send_json({"ok": True, "store": self.bridge.store.snapshot()})
            return
        if path == "/api/eeg-light-test":
            usable, diagnosis = self.bridge.eeg_usable_for_control()
            if not usable:
                self.send_json({"ok": False, "error": diagnosis}, HTTPStatus.BAD_REQUEST)
                return
            color = self.bridge.eeg_palette_color()
            try:
                command = self.bridge.queue_command("light_color", [color["r"], color["g"], color["b"], color["w"]])
            except Exception as exc:
                self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return
            self.send_json({"ok": True, "color": color, "diagnosis": diagnosis, "command": command})
            return
        if path == "/events":
            self.handle_events()
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/config":
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
            try:
                result = self.bridge.apply_store_operation(payload)
            except Exception as exc:
                self.send_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
                return
            self.send_json(result)
            return
        if path != "/api/command":
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        length = int(self.headers.get("Content-Length", "0"))
        payload = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
        action = str(payload.get("action", ""))
        args = payload.get("args", [0, 0, 0, 0])
        if not isinstance(args, list):
            args = [0, 0, 0, 0]
        try:
            command = self.bridge.queue_command(action, args)
        except Exception as exc:
            self.send_json({"error": str(exc)}, HTTPStatus.BAD_REQUEST)
            return
        self.send_json({"ok": True, "command": command})

    def handle_events(self) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        while not self.bridge.stop_event.is_set():
            data = json.dumps(self.bridge.snapshot(), separators=(",", ":"))
            try:
                self.wfile.write(f"data: {data}\n\n".encode("utf-8"))
                self.wfile.flush()
            except OSError:
                return
            time.sleep(STATE_PUSH_INTERVAL)

    def send_bytes(self, data: bytes, content_type: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, payload: dict, status: HTTPStatus = HTTPStatus.OK) -> None:
        self.send_bytes(json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8", status)

    def log_message(self, format: str, *args) -> None:
        return


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read Dream EEG ThinkGear data from Bluetooth serial, send EEG CSV frames to M5Stack, and host a web console."
    )
    parser.add_argument("--source", required=True, help="Bluetooth EEG serial port, for example COM3")
    parser.add_argument("--source-baud", type=int, default=57600, help="Bluetooth EEG baud rate")
    parser.add_argument("--target", required=True, help="M5Stack USB serial port, for example COM6")
    parser.add_argument("--target-baud", type=int, default=115200, help="M5Stack serial baud rate")
    parser.add_argument("--send-rate", type=float, default=20.0, help="EEG CSV send rate in Hz")
    parser.add_argument("--read-chunk", type=int, default=128, help="Maximum source bytes read per loop")
    parser.add_argument("--print-rate", type=float, default=1.0, help="Status print interval in seconds")
    parser.add_argument("--log-file", help="Optional CSV log path, for example logs/eeg-session.csv")
    parser.add_argument("--web-host", default="127.0.0.1", help="Web console host")
    parser.add_argument("--web-port", type=int, default=8765, help="Web console port")
    return parser.parse_args()


def read_uint24_be(values: bytes | bytearray | memoryview) -> int:
    return (values[0] << 16) | (values[1] << 8) | values[2]


def parse_thinkgear_payload(payload: Iterable[int], eeg_frame: DreamEegFrame) -> bool:
    data = bytes(payload)
    index = 0
    updated = False
    while index < len(data):
        code = data[index]
        index += 1
        while code == 0x55 and index < len(data):
            code = data[index]
            index += 1
        value_length = 1
        if code >= 0x80:
            if index >= len(data):
                return False
            value_length = data[index]
            index += 1
        if index + value_length > len(data):
            return False
        value = data[index:index + value_length]
        if code == 0x02 and value_length == 1:
            eeg_frame.poor_signal = value[0]
            updated = True
        elif code == 0x04 and value_length == 1:
            eeg_frame.attention = min(value[0], 100)
            updated = True
        elif code == 0x05 and value_length == 1:
            eeg_frame.meditation = min(value[0], 100)
            updated = True
        elif code == 0x83 and value_length >= 24:
            eeg_frame.eeg_power = [read_uint24_be(value[i * 3:i * 3 + 3]) for i in range(8)]
            updated = True
        index += value_length
    return updated


def build_eeg_csv(seq: int, time_ms: int, eeg_frame: DreamEegFrame) -> str:
    values = [
        "EEG",
        str(seq),
        str(time_ms),
        str(eeg_frame.poor_signal),
        str(eeg_frame.attention),
        str(eeg_frame.meditation),
        *(str(value) for value in eeg_frame.eeg_power),
    ]
    return ",".join(values) + "\n"


def open_serial_port(name: str, baud: int) -> serial.Serial:
    return serial.Serial(port=name, baudrate=baud, timeout=0.02, write_timeout=0.2)


def parse_key_values(line: str) -> dict[str, str]:
    pairs = {}
    for part in line.split():
        if "=" in part:
            key, value = part.split("=", 1)
            pairs[key] = value
    return pairs


def int_value(pairs: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(pairs.get(key, default))
    except ValueError:
        return default


def main() -> int:
    args = parse_args()
    if args.send_rate <= 0:
        raise ValueError("--send-rate must be greater than 0")

    bridge = DreamBridge(args)
    DreamRequestHandler.bridge = bridge
    server = ThreadingHTTPServer((args.web_host, args.web_port), DreamRequestHandler)
    serial_thread = threading.Thread(target=bridge.run_serial_loop, name="dream-serial", daemon=True)
    serial_thread.start()
    bridge.log(f"EVENT=WEB_READY URL=http://{args.web_host}:{args.web_port}/")

    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        bridge.stop_event.set()
        server.server_close()
        serial_thread.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nEVENT=PC_BRIDGE_STOPPED")
