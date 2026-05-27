#!/usr/bin/env python3
"""Dream PC bridge: Bluetooth ThinkGear serial -> M5Stack USB serial + web console."""

from __future__ import annotations

import argparse
import csv
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
    button, input[type="color"], input[type="range"], input[type="number"] {
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
    .input-row {
      display: grid;
      grid-template-columns: 56px minmax(0, 1fr) 74px;
      gap: 10px;
      align-items: center;
    }
    .step-row {
      display: grid;
      grid-template-columns: 44px minmax(0, 1fr) 82px;
      gap: 10px;
      align-items: center;
    }
    input[type="color"] {
      width: 56px;
      height: 40px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--surface);
      padding: 4px;
    }
    input[type="range"] { width: 100%; accent-color: var(--accent); }
    input[type="number"] {
      width: 100%;
      min-height: 38px;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--surface);
      color: var(--ink);
      padding: 0 8px;
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
    @media (max-width: 1100px) {
      .app { grid-template-columns: 1fr; }
      aside { border-right: 0; border-bottom: 1px solid var(--line); }
      .layout { grid-template-columns: 1fr; }
      .metric-grid.three, .metric-grid.four, .freq-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
    @media (max-width: 680px) {
      main, aside { padding: 14px; }
      .hero, .section-head, .control-title { align-items: flex-start; flex-direction: column; }
      .button-row, .button-row.two, .metric-grid.three, .metric-grid.four, .freq-grid { grid-template-columns: 1fr; }
      .input-row, .step-row { grid-template-columns: 1fr; }
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
          <h1>实时状态与机器控制</h1>
          <p class="subtle">没有收到真实数据时，字段会保持为空，不用模拟值填充。</p>
        </div>
        <span id="eegStatus" class="pill warn">等待脑电</span>
      </div>
      <div class="layout">
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
          <section>
            <div class="section-head"><h2>Microduino 执行状态</h2><span id="micAge" class="pill neutral">-- ms</span></div>
            <div class="metric-grid four">
              <div class="metric"><div class="label">灯光</div><div class="value small" id="lightMode">--</div><div class="label" id="lightLevel">level --</div></div>
              <div class="metric"><div class="label">雾机 / 继电器</div><div class="value small" id="relayState">--</div><div class="label" id="relayEnabled">--</div></div>
              <div class="metric"><div class="label">步进电机</div><div class="value small" id="stepperState">--</div><div class="label" id="stepperEnabled">--</div></div>
              <div class="metric"><div class="label">安全状态</div><div class="value small" id="safetyState">--</div><div class="label" id="lastAction">action --</div></div>
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
            <div class="section-head"><h2>机器控制</h2><span id="commandStatus" class="pill warn">等待串口</span></div>
            <div class="button-row">
              <button class="good" data-action="system_enable">系统开启</button>
              <button class="danger" data-action="system_disable">系统关闭</button>
              <button class="danger" data-action="all_stop">全部停止</button>
            </div>
            <div class="control-block">
              <div class="control-title"><h2>灯光</h2><button data-action="light_auto">自动脑电</button></div>
              <div class="input-row">
                <input id="lightColor" type="color" value="#5f7fcd" aria-label="灯光颜色">
                <input id="whiteLevel" type="range" min="0" max="255" value="0" aria-label="白光亮度">
                <input id="whiteNumber" type="number" min="0" max="255" value="0" aria-label="白光数值">
              </div>
              <div class="button-row two">
                <button class="primary" data-action="light_color">发送颜色</button>
                <button data-action="light_off">灯光关闭</button>
              </div>
            </div>
            <div class="control-block">
              <div class="control-title"><h2>雾机 / 继电器</h2><span id="relayHint" class="pill neutral">--</span></div>
              <div class="button-row two">
                <button class="good" data-action="relay_on">开启</button>
                <button data-action="relay_off">停止</button>
              </div>
            </div>
            <div class="control-block">
              <div class="control-title"><h2>步进电机</h2><span id="stepperHint" class="pill neutral">--</span></div>
              <div class="step-row">
                <span class="label">步数</span>
                <input id="stepperSteps" type="range" min="20" max="800" value="200" aria-label="步进步数">
                <input id="stepperNumber" type="number" min="20" max="800" value="200" aria-label="步进步数数值">
              </div>
              <div class="button-row">
                <button class="primary" data-action="stepper_forward">正向触发</button>
                <button class="primary" data-action="stepper_backward">反向触发</button>
                <button data-action="stepper_stop">停止电机</button>
              </div>
            </div>
            <div id="notice" class="notice">等待真实串口连接。</div>
          </section>
          <section>
            <div class="section-head"><h2>事件日志</h2><button id="clearLog">清空本地显示</button></div>
            <div id="log" class="log">等待真实事件...</div>
          </section>
        </div>
      </div>
    </main>
  </div>
  <script>
    const lightModes = ["OFF", "IDLE", "EEG", "BAD", "TIMEOUT", "MANUAL"];
    const stepperStates = ["DISABLED", "IDLE", "MOVING", "BREATH", "LIMIT", "FAULT"];
    const relayStates = ["OFF", "ARMING", "ON", "COOL", "FAULT"];
    const safetyStates = ["NORMAL", "SIGNAL", "TIMEOUT", "ESTOP", "FAULT"];
    const controlActions = ["NONE", "SYSTEM_ENABLE", "SYSTEM_DISABLE", "LIGHT_AUTO", "LIGHT_COLOR", "LIGHT_OFF", "RELAY_ON", "RELAY_OFF", "STEPPER_FORWARD", "STEPPER_BACKWARD", "STEPPER_STOP", "ALL_STOP"];
    const EEG_POWER_FIELDS = ["delta", "theta", "lowAlpha", "highAlpha", "lowBeta", "highBeta", "lowGamma", "midGamma"];
    const ids = {};
    const buttonLabels = new WeakMap();
    let localNotice = "";
    let suppressLogRender = false;
    let commandFeedbackUntil = 0;
    let commandFeedbackTimer = 0;
    let latestCommandReady = false;
    document.querySelectorAll("[id]").forEach((el) => ids[el.id] = el);
    document.querySelectorAll("button[data-action], #clearLog").forEach((button) => buttonLabels.set(button, button.textContent));

    const setText = (id, value) => { ids[id].textContent = value ?? "--"; };
    const realText = (seen, value, suffix = "") => seen && value !== undefined && value !== null ? `${value}${suffix}` : "--";
    const modeText = (seen, values, index) => seen ? (values[index] ?? `UNKNOWN ${index}`) : "--";
    const actionText = (seen, index) => seen ? `action ${controlActions[index] ?? `UNKNOWN ${index}`}` : "action --";
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
    const setNotice = (text) => {
      localNotice = text;
      ids.notice.textContent = text;
    };
    const commandArgs = (action) => {
      if (action === "light_color") {
        const hex = ids.lightColor.value.slice(1);
        return [
          parseInt(hex.slice(0, 2), 16),
          parseInt(hex.slice(2, 4), 16),
          parseInt(hex.slice(4, 6), 16),
          Number(ids.whiteNumber.value || 0)
        ];
      }
      if (action === "stepper_forward" || action === "stepper_backward") {
        return [Number(ids.stepperNumber.value || 200), 0, 0, 0];
      }
      return [0, 0, 0, 0];
    };
    const setCommandFeedback = (state, text, duration = 1600) => {
      pill(ids.commandStatus, state, text);
      commandFeedbackUntil = Date.now() + duration;
      window.clearTimeout(commandFeedbackTimer);
      commandFeedbackTimer = window.setTimeout(() => setCommandReady(latestCommandReady), duration);
    };
    const setButtonPending = (button, pending) => {
      button.classList.toggle("is-pending", pending);
      button.disabled = pending;
      button.setAttribute("aria-busy", pending ? "true" : "false");
    };
    const setCommandReady = (enabled) => {
      latestCommandReady = enabled;
      if (Date.now() < commandFeedbackUntil) return;
      pill(ids.commandStatus, enabled ? "ok" : "warn", enabled ? "可发送" : "等待串口");
    };
    const renderLogs = (logs) => {
      if (suppressLogRender) return;
      if (logs && logs.length) {
        ids.log.textContent = logs.join("\n");
      } else {
        ids.log.textContent = "等待真实事件...";
      }
    };
    const sendCommand = async (button) => {
      const action = button.dataset.action;
      const label = buttonLabels.get(button) || action;
      setButtonPending(button, true);
      setCommandFeedback("warn", "发送中", 30000);
      setNotice(`正在发送：${label}`);
      try {
        const response = await fetch("/api/command", {
          method: "POST",
          headers: {"Content-Type": "application/json"},
          body: JSON.stringify({action, args: commandArgs(action)})
        });
        const payload = await response.json();
        if (!response.ok) {
          setCommandFeedback("bad", "失败");
          setNotice(`命令未发送：${payload.error || response.status}`);
          return;
        }
        setCommandFeedback("ok", "已发送");
        setNotice(`命令已交给串口队列：${payload.command}`);
      } catch (error) {
        setCommandFeedback("bad", "失败");
        setNotice(`命令未发送：${error}`);
      } finally {
        setButtonPending(button, false);
      }
    };
    document.querySelectorAll("button[data-action]").forEach((button) => {
      button.addEventListener("click", () => sendCommand(button));
    });
    ids.clearLog.addEventListener("click", () => {
      suppressLogRender = true;
      ids.log.textContent = "";
      setCommandFeedback("ok", "已清空", 1200);
      setNotice("事件日志显示已清空。");
      setTimeout(() => suppressLogRender = false, 2500);
    });
    ids.whiteLevel.addEventListener("input", () => ids.whiteNumber.value = ids.whiteLevel.value);
    ids.whiteNumber.addEventListener("input", () => ids.whiteLevel.value = ids.whiteNumber.value);
    ids.stepperSteps.addEventListener("input", () => ids.stepperNumber.value = ids.stepperSteps.value);
    ids.stepperNumber.addEventListener("input", () => ids.stepperSteps.value = ids.stepperNumber.value);

    const update = (state) => {
      const eegSeen = Boolean(state.eeg.seen);
      const m5Seen = Boolean(state.m5.seen);
      const micSeen = Boolean(state.mic.seen && state.m5.micStatus === "YES");
      const serialReady = Boolean(state.bridge.sourceOpen && state.bridge.targetOpen);
      const commandReady = Boolean(state.bridge.targetOpen);
      pill(ids.webStatus, "ok", "已连接");
      pill(ids.sourceStatus, serialReady ? "ok" : "warn", serialReady ? "串口在线" : "等待串口");
      pill(ids.m5Status, m5Seen ? "ok" : "warn", m5Seen ? "在线" : "等待");
      pill(ids.micStatus, micSeen ? "ok" : "warn", micSeen ? "在线" : "等待");
      pill(ids.eegStatus, eegSeen ? (state.eeg.ageMs < 1200 ? "ok" : "warn") : "warn", eegSeen ? "真实数据" : "等待脑电");
      pill(ids.systemEnabled, micSeen ? (state.mic.systemEnabled ? "ok" : "bad") : "neutral", micSeen ? (state.mic.systemEnabled ? "系统已开启" : "系统关闭") : "--");
      setCommandReady(commandReady);

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
      setText("lightLevel", micSeen ? `level ${state.mic.lightLevel}` : "level --");
      setText("relayState", modeText(micSeen, relayStates, state.mic.relayState));
      setText("stepperState", modeText(micSeen, stepperStates, state.mic.stepperState));
      setText("safetyState", modeText(micSeen, safetyStates, state.mic.safetyState));
      setText("lastAction", actionText(micSeen, state.mic.lastControlAction));
      setText("relayEnabled", micSeen ? (state.mic.relayOutputEnabled ? "固件输出已启用" : "固件输出关闭") : "--");
      setText("stepperEnabled", micSeen ? (state.mic.stepperOutputEnabled ? "固件输出已启用" : "固件输出关闭") : "--");
      pill(ids.relayHint, micSeen ? (state.mic.relayOutputEnabled ? "ok" : "warn") : "neutral", micSeen ? (state.mic.relayOutputEnabled ? "可输出" : "输出关闭") : "--");
      pill(ids.stepperHint, micSeen ? (state.mic.stepperOutputEnabled ? "ok" : "warn") : "neutral", micSeen ? (state.mic.stepperOutputEnabled ? "可输出" : "输出关闭") : "--");

      setText("sentFrames", realText(state.bridge.targetOpen, state.stats.sentFrames));
      setText("validPackets", realText(eegSeen, state.stats.validPackets));
      setText("checksumErrors", eegSeen ? `checksum ${state.stats.checksumErrors}` : "checksum --");
      setText("espNowStats", m5Seen ? `tx ${state.m5.espNowTx} / fail ${state.m5.espNowFail}` : "--");
      setText("m5Command", m5Seen ? `cmd rx ${state.m5.cmdRx} / tx ${state.m5.cmdTx}` : "cmd --");
      setText("micPackets", micSeen ? `rx ${state.mic.rxCount} / drop ${state.mic.dropCount}` : "--");
      setText("micControl", micSeen ? `control ${state.mic.controlRxCount}` : "control --");
      if (!localNotice) {
        ids.notice.textContent = commandReady ? "控制指令会通过 M5Stack 串口转发。" : "等待 M5Stack 串口打开。";
      }
      renderLogs(state.bridge.logs);
    };

    const events = new EventSource("/events");
    events.onmessage = (event) => update(JSON.parse(event.data));
    events.onerror = () => {
      pill(ids.webStatus, "bad", "断开");
      setCommandReady(false);
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
    seen: bool = False
    last_update_time: float = 0.0


@dataclass
class MicStatus:
    rx_count: int = 0
    drop_count: int = 0
    timeout_count: int = 0
    light_mode: int = 0
    light_level: int = 0
    stepper_state: int = 0
    relay_state: int = 0
    safety_state: int = 2
    control_rx_count: int = 0
    last_control_action: int = 0
    manual_light_enabled: int = 0
    relay_output_enabled: int = 0
    stepper_output_enabled: int = 0
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

    def run_serial_loop(self) -> None:
        send_interval = 1.0 / self.args.send_rate
        next_send_time = time.monotonic() + send_interval
        next_print_time = time.monotonic() + self.args.print_rate
        self.open_log_writer()
        try:
            with open_serial_port(self.args.source, self.args.source_baud) as source, open_serial_port(self.args.target, self.args.target_baud) as target:
                with self.lock:
                    self.runtime.source_open = True
                    self.runtime.target_open = True
                self.log(
                    "EVENT=DREAM_BOOT ROLE=pcBridge "
                    f"VERSION={PC_BRIDGE_VERSION} SOURCE={self.args.source}@{self.args.source_baud} "
                    f"TARGET={self.args.target}@{self.args.target_baud} SEND_RATE={self.args.send_rate} "
                    f"WEB=http://127.0.0.1:{self.args.web_port}/"
                )
                target.reset_input_buffer()

                while not self.stop_event.is_set():
                    raw = source.read(max(1, self.args.read_chunk))
                    if raw:
                        with self.lock:
                            self.stats.raw_bytes += len(raw)
                        for value in raw:
                            with self.lock:
                                self.parser.feed(value, self.eeg_frame, self.stats)

                    now = time.monotonic()
                    with self.lock:
                        should_send = self.eeg_frame.seen and now >= next_send_time
                    if should_send:
                        self.send_eeg_frame(target, now)
                        next_send_time += send_interval
                        if now - next_send_time > send_interval:
                            next_send_time = now + send_interval

                    self.send_pending_commands(target)
                    self.drain_target_log(target)

                    if now >= next_print_time:
                        self.print_status()
                        next_print_time = now + self.args.print_rate
                        if self.log_file is not None:
                            self.log_file.flush()
        except Exception as exc:
            with self.lock:
                self.runtime.last_error = str(exc)
                self.runtime.source_open = False
                self.runtime.target_open = False
            self.log(f"EVENT=PC_BRIDGE_ERROR ERROR={exc}")
            raise
        finally:
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
            target.write(line.encode("ascii"))
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
                if self.m5_status.mic_status == "YES":
                    self.mic_status.seen = True
                    self.mic_status.last_update_time = now
                    self.mic_status.rx_count = int_value(pairs, "MIC_RX")
                    self.mic_status.drop_count = int_value(pairs, "MIC_DROP")
                    self.mic_status.timeout_count = int_value(pairs, "MIC_TIMEOUT")
                    self.mic_status.light_mode = int_value(pairs, "MIC_LIGHT_MODE")
                    self.mic_status.light_level = int_value(pairs, "MIC_LIGHT_LEVEL")
                    self.mic_status.stepper_state = int_value(pairs, "MIC_STEPPER")
                    self.mic_status.relay_state = int_value(pairs, "MIC_RELAY")
                    self.mic_status.safety_state = int_value(pairs, "MIC_SAFETY", 2)
                    self.mic_status.control_rx_count = int_value(pairs, "MIC_CONTROL_RX")
                    self.mic_status.last_control_action = int_value(pairs, "MIC_LAST_ACTION")
                    self.mic_status.manual_light_enabled = int_value(pairs, "MIC_MANUAL_LIGHT")
                    self.mic_status.relay_output_enabled = int_value(pairs, "MIC_RELAY_ENABLED")
                    self.mic_status.stepper_output_enabled = int_value(pairs, "MIC_STEPPER_ENABLED")
                    self.mic_status.system_enabled = int_value(pairs, "MIC_SYSTEM_ENABLED")
                else:
                    self.mic_status.seen = False
        if line.startswith("EVENT=CONTROL") or line.startswith("EVENT=SERIAL_PARSE_FAIL"):
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
        clean_args = [max(0, min(65535, int(value))) for value in (args + [0, 0, 0, 0])[:4]]
        with self.lock:
            if not self.runtime.target_open:
                raise RuntimeError("target serial is not open")
            seq = self.command_seq
            self.command_seq += 1
            time_ms = int((time.monotonic() - self.start_time) * 1000)
        line = f"CMD,{seq},{time_ms},{action_name},{clean_args[0]},{clean_args[1]},{clean_args[2]},{clean_args[3]}\n"
        self.command_queue.put(line)
        return line.strip()

    def snapshot(self) -> dict:
        now = time.monotonic()
        with self.lock:
            eeg_age = int((now - self.eeg_frame.last_source_time) * 1000) if self.eeg_frame.seen else -1
            m5_age = int((now - self.m5_status.last_update_time) * 1000) if self.m5_status.seen else -1
            mic_age = int((now - self.mic_status.last_update_time) * 1000) if self.mic_status.seen else -1
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
                    "seen": self.eeg_frame.seen,
                    "ageMs": eeg_age,
                    "poorSignal": self.eeg_frame.poor_signal,
                    "attention": self.eeg_frame.attention,
                    "meditation": self.eeg_frame.meditation,
                    "eegPower": list(self.eeg_frame.eeg_power),
                },
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
                },
                "mic": {
                    "seen": self.mic_status.seen,
                    "ageMs": mic_age,
                    "rxCount": self.mic_status.rx_count,
                    "dropCount": self.mic_status.drop_count,
                    "timeoutCount": self.mic_status.timeout_count,
                    "lightMode": self.mic_status.light_mode,
                    "lightLevel": self.mic_status.light_level,
                    "stepperState": self.mic_status.stepper_state,
                    "relayState": self.mic_status.relay_state,
                    "safetyState": self.mic_status.safety_state,
                    "controlRxCount": self.mic_status.control_rx_count,
                    "lastControlAction": self.mic_status.last_control_action,
                    "manualLightEnabled": self.mic_status.manual_light_enabled,
                    "relayOutputEnabled": self.mic_status.relay_output_enabled,
                    "stepperOutputEnabled": self.mic_status.stepper_output_enabled,
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
        if path == "/events":
            self.handle_events()
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
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
