// @ts-nocheck
import "./styles.css";
import {
  buildDistanceMap,
  buildScene,
  getCenterNode,
} from "./scene/hex-grid";
import {
  drawAddressLabels,
  drawChannelLabels,
  drawDistanceLabels,
  drawHoverNode,
  drawJet,
  drawOutlineGlow,
  drawStepperGlow,
} from "./render/canvas";
import {
  RELAY_CHANNEL_COUNT,
  buildRelayStates,
  buildRelayWriteMultipleFrame,
  findPreferredSerialPort,
  getRelayMappedNodeIds,
  parseRelayCommandInput,
  relayStatesEqual,
  rememberSerialPortRole,
} from "./devices/relay";
import {
  MIDI_CC_BACK,
  MIDI_CC_NEXT,
  MIDI_CC_PAUSE,
  MIDI_CC_PLAY,
  MIDI_CC_SPEED,
  MIDI_CC_STEPPER_1,
  MIDI_CC_STEPPER_1_ATTACK,
  MIDI_CC_STEPPER_1_DECAY,
  MIDI_CC_STEPPER_1_RELEASE,
  MIDI_CC_STEPPER_1_SUSTAIN,
  MIDI_NOTE_ALL_OFF,
  MIDI_NOTE_ALL_ON,
  MIDI_NOTE_STEPPER_1_ENV,
  MIDI_PAD_MAP,
  connectAllMidiInputs as connectMidiAccessInputs,
  disconnectMidiInputs,
  formatMidiMessage,
  getMidiInputLabel,
} from "./devices/midi";

      const canvas = document.getElementById("hex-canvas");
      const context = canvas.getContext("2d");
      const ringsInput = document.getElementById("rings");
      const ringCount = document.getElementById("ring-count");
      const resetButton = document.getElementById("reset-button");
      const jetModeSelect = document.getElementById("jet-mode-select");
      const allOnButton = document.getElementById("all-on-button");
      const allOffButton = document.getElementById("all-off-button");
      const labelModeSelect = document.getElementById("label-mode-select");
      const sequenceSelect = document.getElementById("sequence-select");
      const playPauseButton = document.getElementById("play-pause-button");
      const loopToggleButton = document.getElementById("loop-toggle-button");
      const speedSlider = document.getElementById("speed-slider");
      const speedReadout = document.getElementById("speed-readout");
      const serialConnectButton = document.getElementById(
        "serial-connect-button",
      );
      const stepperConnectButton = document.getElementById(
        "stepper-connect-button",
      );
      const stepperHomeButton = document.getElementById("stepper-home-button");
      const stepperPositionSlider = document.getElementById(
        "stepper-position-slider",
      );
      const stepperPositionReadout = document.getElementById(
        "stepper-position-readout",
      );
      const stepperHomedStateText = document.getElementById(
        "stepper-homed-state",
      );
      const stepperTravelStepsText = document.getElementById(
        "stepper-travel-steps",
      );
      const stepperEnvelopePath = document.getElementById(
        "stepper-envelope-path",
      );
      const stepperEnvelopeFill = document.getElementById(
        "stepper-envelope-fill",
      );
      const stepperEnvelopeSweep = document.getElementById(
        "stepper-envelope-sweep",
      );
      const stepperEnvelopeMarker = document.getElementById(
        "stepper-envelope-marker",
      );
      const stepperAttackReadout = document.getElementById(
        "stepper-attack-readout",
      );
      const stepperDecayReadout = document.getElementById(
        "stepper-decay-readout",
      );
      const stepperSustainReadout = document.getElementById(
        "stepper-sustain-readout",
      );
      const stepperReleaseReadout = document.getElementById(
        "stepper-release-readout",
      );
      const editorToggleButton = document.getElementById("editor-toggle-button");
      const editorPanel = document.getElementById("editor-panel");
      const animationEditor = document.getElementById("animation-editor");
      const restoreOriginalButton = document.getElementById(
        "restore-original-button",
      );
      const editorError = document.getElementById("editor-error");
      const midiConnectButton = document.getElementById("midi-connect-button");
      const midiStatus = document.getElementById("midi-status");
      const serialStatus = document.getElementById("serial-status");
      const stepperStatus = document.getElementById("stepper-status");
      const relayCommandForm = document.getElementById("relay-command-form");
      const relayCommandInput = document.getElementById("relay-command-input");
      const relayCommandSendButton = document.getElementById(
        "relay-command-send-button",
      );
      const stepperCommandForm = document.getElementById("stepper-command-form");
      const stepperCommandInput = document.getElementById(
        "stepper-command-input",
      );
      const stepperCommandSendButton = document.getElementById(
        "stepper-command-send-button",
      );
      const logPane = document.getElementById("log-pane");
      const logPaneToggleButton = document.getElementById("log-pane-toggle");
      const clearDeviceLogsButton = document.getElementById(
        "clear-device-logs-button",
      );
      const deviceLogStreams = {
        midi: document.getElementById("midi-log-stream"),
        relay: document.getElementById("relay-log-stream"),
        stepper: document.getElementById("stepper-log-stream"),
      };
      const deviceLogCounts = {
        midi: document.getElementById("midi-log-count"),
        relay: document.getElementById("relay-log-count"),
        stepper: document.getElementById("stepper-log-count"),
      };
      const outlineStats = document.getElementById("outline-stats");
      const spokeStats = document.getElementById("spoke-stats");
      const STORAGE_KEY = "hexagon-rings-state";
      const RELAY_PORT_KEY = `${STORAGE_KEY}:relay-port`;
      const STEPPER_PORT_KEY = `${STORAGE_KEY}:stepper-port`;
      const LOG_PANE_COLLAPSED_KEY = `${STORAGE_KEY}:log-pane-collapsed`;
      const HIT_RADIUS = 10;
      const DEFAULT_RINGS = 2;
      const DEVICE_LOG_LIMIT = 250;
      const STEPPER_SERIAL_BAUD = 115200;
      const STEPPER_SEND_DELAY_MS = 75;
      const STEPPER_ENVELOPE_SEND_DELAY_MS = 20;
      const activeNodes = new Set();
      const knownNodeIds = new Set();
      let scene = null;
      let hoveredNodeId = null;
      let jetMode = "all";
      let labelMode = "address";
      const customScripts = {};
      let selectedSequenceId = "ripple";
      let animationSpeed = 12;
      let animationLoopEnabled = true;
      let animationFrames = [];
      let animationFrameIndex = 0;
      let animationTimerId = null;
      let midiAccess = null;
      const midiInputs = new Map();
      const midiHeldNotes = new Set();
      const deviceLogs = {
        midi: [],
        relay: [],
        stepper: [],
      };
      let relayPort = null;
      let relayReader = null;
      let relaySyncInProgress = false;
      let relayStateQueue = [];
      let relayLastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
      let relayStatusMessage = "Relay sync disconnected";
      let stepperPort = null;
      let stepperReader = null;
      let stepperStatusMessage = "Stepper disconnected";
      let stepperBasePositionPercent = 50;
      let stepperPositionPercent = 50;
      let stepperHomed = false;
      let stepperTravelSteps = null;
      let stepperQueuedPosition = null;
      let stepperPositionSendInProgress = false;
      let stepperPositionSendTimerId = null;
      let stepperLastSendAtMs = -Infinity;
      let stepperConnectInProgress = false;
      let stepperEnvelopeFrameId = null;
      const stepperEnvelope = {
        active: false,
        noteHeld: false,
        phase: "idle",
        attackMs: 180,
        decayMs: 220,
        sustainLevel: 0.55,
        releaseMs: 320,
        originPercent: 50,
        velocityScale: 1,
        currentLevel: 0,
        phaseStartMs: 0,
        phaseStartLevel: 0,
      };

      function angleFromCenter(node) {
        return Math.atan2(
          node.y - canvas.clientHeight / 2,
          node.x - canvas.clientWidth / 2,
        );
      }

      function positiveAngleDelta(fromAngle, toAngle) {
        const tau = Math.PI * 2;
        return (toAngle - fromAngle + tau) % tau;
      }

      function sortByAngle(nodes) {
        return [...nodes].sort(
          (left, right) => angleFromCenter(left) - angleFromCenter(right),
        );
      }

      function dedupeFrames(frames) {
        const deduped = [];
        let previousKey = null;

        for (const frame of frames) {
          const key = [...frame].sort().join("|");
          if (key !== previousKey) {
            deduped.push(new Set(frame));
            previousKey = key;
          }
        }

        return deduped;
      }

      function updateSerialUi(mappedCount = 0) {
        const serialSupported = "serial" in navigator;
        serialConnectButton.disabled = !serialSupported;
        relayCommandInput.disabled = !serialSupported || relayPort === null;
        relayCommandSendButton.disabled = !serialSupported || relayPort === null;
        serialConnectButton.classList.toggle("connected", relayPort !== null);
        if (!serialSupported) {
          serialStatus.textContent = "Web Serial unavailable in this browser";
          return;
        }

        serialStatus.textContent = `${relayStatusMessage} (${mappedCount}/${RELAY_CHANNEL_COUNT} mapped)`;
      }

      function updateStepperReadout() {
        stepperPositionReadout.value = `${stepperPositionPercent.toFixed(1)}%`;
      }

      function updateStepperTravelReadout() {
        stepperTravelStepsText.textContent =
          stepperTravelSteps === null
            ? "Unknown"
            : `${stepperTravelSteps.toLocaleString()} steps`;
      }

      function canControlStepperPosition() {
        return stepperPort !== null && stepperHomed;
      }

      function updateStepperHomedReadout() {
        if (stepperPort === null) {
          stepperHomedStateText.textContent = "Unknown";
          return;
        }

        stepperHomedStateText.textContent = stepperHomed ? "Yes" : "No";
      }

      function formatEnvelopeTime(ms) {
        return `${Math.round(ms)}ms`;
      }

      function getStepperEnvelopeGraphLayout() {
        const graphLeft = 14;
        const graphRight = 246;
        const graphBottom = 78;
        const graphTop = 14;
        const graphWidth = graphRight - graphLeft;
        const graphHeight = graphBottom - graphTop;
        const sustainWidthWeight = 220;
        const total =
          stepperEnvelope.attackMs +
          stepperEnvelope.decayMs +
          stepperEnvelope.releaseMs +
          sustainWidthWeight;

        const attackX = graphLeft + graphWidth * (stepperEnvelope.attackMs / total);
        const decayX =
          attackX + graphWidth * (stepperEnvelope.decayMs / total);
        const sustainEndX =
          decayX + graphWidth * (sustainWidthWeight / total);
        const sustainY =
          graphBottom - graphHeight * stepperEnvelope.sustainLevel;

        return {
          graphLeft,
          graphRight,
          graphBottom,
          graphTop,
          attackX,
          decayX,
          sustainEndX,
          sustainY,
          sustainWidthWeight,
        };
      }

      function getStepperEnvelopeGraphPoint(now = performance.now()) {
        const {
          graphLeft,
          graphRight,
          graphBottom,
          graphTop,
          attackX,
          decayX,
          sustainEndX,
          sustainY,
          sustainWidthWeight,
        } = getStepperEnvelopeGraphLayout();
        const elapsed = Math.max(0, now - stepperEnvelope.phaseStartMs);

        if (!stepperEnvelope.active) {
          return { x: graphLeft, y: graphBottom };
        }

        if (stepperEnvelope.phase === "attack") {
          const progress =
            stepperEnvelope.attackMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.attackMs, 1);
          return {
            x: graphLeft + (attackX - graphLeft) * progress,
            y: graphBottom + (graphTop - graphBottom) * progress,
          };
        }

        if (stepperEnvelope.phase === "decay") {
          const progress =
            stepperEnvelope.decayMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.decayMs, 1);
          return {
            x: attackX + (decayX - attackX) * progress,
            y: graphTop + (sustainY - graphTop) * progress,
          };
        }

        if (stepperEnvelope.phase === "sustain") {
          const progress = Math.min(elapsed / sustainWidthWeight, 1);
          return {
            x: decayX + (sustainEndX - decayX) * progress,
            y: sustainY,
          };
        }

        if (stepperEnvelope.phase === "release") {
          const progress =
            stepperEnvelope.releaseMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.releaseMs, 1);
          return {
            x: sustainEndX + (graphRight - sustainEndX) * progress,
            y: sustainY + (graphBottom - sustainY) * progress,
          };
        }

        return { x: graphLeft, y: graphBottom };
      }

      function updateStepperEnvelopeUi(now = performance.now()) {
        const sustainPercent = Math.round(stepperEnvelope.sustainLevel * 100);
        stepperAttackReadout.textContent =
          `A ${formatEnvelopeTime(stepperEnvelope.attackMs)}`;
        stepperDecayReadout.textContent =
          `D ${formatEnvelopeTime(stepperEnvelope.decayMs)}`;
        stepperSustainReadout.textContent = `S ${sustainPercent}%`;
        stepperReleaseReadout.textContent =
          `R ${formatEnvelopeTime(stepperEnvelope.releaseMs)}`;

        const {
          graphLeft,
          graphRight,
          graphBottom,
          graphTop,
          attackX,
          decayX,
          sustainEndX,
          sustainY,
        } = getStepperEnvelopeGraphLayout();
        const graphHeight = graphBottom - graphTop;

        stepperEnvelopePath.setAttribute(
          "d",
          `M ${graphLeft} ${graphBottom} ` +
            `L ${attackX.toFixed(1)} ${graphTop} ` +
            `L ${decayX.toFixed(1)} ${sustainY.toFixed(1)} ` +
            `L ${sustainEndX.toFixed(1)} ${sustainY.toFixed(1)} ` +
            `L ${graphRight} ${graphBottom}`,
        );

        const point = getStepperEnvelopeGraphPoint(now);
        const fillSegments = [`M ${graphLeft} ${graphBottom}`];
        if (stepperEnvelope.active) {
          if (stepperEnvelope.phase === "attack") {
            fillSegments.push(`L ${point.x.toFixed(1)} ${point.y.toFixed(1)}`);
          } else if (stepperEnvelope.phase === "decay") {
            fillSegments.push(`L ${attackX.toFixed(1)} ${graphTop.toFixed(1)}`);
            fillSegments.push(`L ${point.x.toFixed(1)} ${point.y.toFixed(1)}`);
          } else if (stepperEnvelope.phase === "sustain") {
            fillSegments.push(`L ${attackX.toFixed(1)} ${graphTop.toFixed(1)}`);
            fillSegments.push(`L ${decayX.toFixed(1)} ${sustainY.toFixed(1)}`);
            fillSegments.push(`L ${point.x.toFixed(1)} ${point.y.toFixed(1)}`);
          } else if (stepperEnvelope.phase === "release") {
            fillSegments.push(`L ${attackX.toFixed(1)} ${graphTop.toFixed(1)}`);
            fillSegments.push(`L ${decayX.toFixed(1)} ${sustainY.toFixed(1)}`);
            fillSegments.push(`L ${sustainEndX.toFixed(1)} ${sustainY.toFixed(1)}`);
            fillSegments.push(`L ${point.x.toFixed(1)} ${point.y.toFixed(1)}`);
          }
        } else {
          fillSegments.push(`L ${graphLeft} ${graphBottom}`);
        }
        fillSegments.push(`L ${point.x.toFixed(1)} ${graphBottom.toFixed(1)}`);
        fillSegments.push("Z");
        stepperEnvelopeFill.setAttribute("d", fillSegments.join(" "));

        const envelopeLevel = stepperEnvelope.active
          ? stepperEnvelope.currentLevel
          : 0;
        const actualLevel = envelopeLevel * stepperEnvelope.velocityScale;
        const markerY = graphBottom - graphHeight * actualLevel;
        stepperEnvelopeSweep.setAttribute("x1", point.x.toFixed(1));
        stepperEnvelopeSweep.setAttribute("x2", point.x.toFixed(1));
        stepperEnvelopeSweep.setAttribute("y1", point.y.toFixed(1));
        stepperEnvelopeSweep.setAttribute("y2", graphBottom.toFixed(1));
        stepperEnvelopeMarker.setAttribute("cx", point.x.toFixed(1));
        stepperEnvelopeMarker.setAttribute("cy", markerY.toFixed(1));
      }

      function applyStepperOutputPosition(
        percent,
        { send = true, save = true, sendDelayMs = STEPPER_SEND_DELAY_MS } = {},
      ) {
        stepperPositionPercent = Math.min(Math.max(percent, 0), 100);
        stepperPositionSlider.value = stepperPositionPercent.toFixed(1);
        updateStepperReadout();
        if (save) {
          saveState();
        }
        if (scene) {
          render();
        }
        if (send && canControlStepperPosition()) {
          queueStepperPositionSend(sendDelayMs);
        }
      }

      function applyStepperEnvelopeOutput(
        send = true,
        sendDelayMs = STEPPER_ENVELOPE_SEND_DELAY_MS,
      ) {
        const outputPercent =
          stepperEnvelope.originPercent +
          (100 - stepperEnvelope.originPercent) *
            stepperEnvelope.velocityScale *
            stepperEnvelope.currentLevel;
        applyStepperOutputPosition(outputPercent, {
          send,
          save: false,
          sendDelayMs,
        });
      }

      function setStepperBasePosition(
        percent,
        { send = true, sendDelayMs = STEPPER_SEND_DELAY_MS } = {},
      ) {
        stepperBasePositionPercent = Math.min(Math.max(percent, 0), 100);
        if (!stepperEnvelope.active) {
          applyStepperOutputPosition(stepperBasePositionPercent, {
            send,
            save: false,
            sendDelayMs,
          });
        }
        saveState();
      }

      function updateStepperEnvelopePhase(phase, now) {
        stepperEnvelope.phase = phase;
        stepperEnvelope.phaseStartMs = now;
        stepperEnvelope.phaseStartLevel = stepperEnvelope.currentLevel;
        updateStepperEnvelopeUi();
      }

      function stopStepperEnvelope() {
        stepperEnvelope.active = false;
        stepperEnvelope.noteHeld = false;
        stepperEnvelope.phase = "idle";
        stepperEnvelope.velocityScale = 1;
        stepperEnvelope.currentLevel = 0;
        stepperEnvelope.phaseStartLevel = 0;
        if (stepperEnvelopeFrameId !== null) {
          window.cancelAnimationFrame(stepperEnvelopeFrameId);
          stepperEnvelopeFrameId = null;
        }
        applyStepperOutputPosition(stepperEnvelope.originPercent, {
          send: true,
          save: false,
          sendDelayMs: STEPPER_ENVELOPE_SEND_DELAY_MS,
        });
        updateStepperEnvelopeUi();
        saveState();
      }

      function tickStepperEnvelope(now) {
        if (!stepperEnvelope.active) {
          stepperEnvelopeFrameId = null;
          return;
        }

        const elapsed = now - stepperEnvelope.phaseStartMs;
        if (stepperEnvelope.phase === "attack") {
          const progress =
            stepperEnvelope.attackMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.attackMs, 1);
          stepperEnvelope.currentLevel =
            stepperEnvelope.phaseStartLevel +
            (1 - stepperEnvelope.phaseStartLevel) * progress;
          if (progress >= 1) {
            stepperEnvelope.currentLevel = 1;
            updateStepperEnvelopePhase("decay", now);
          }
        } else if (stepperEnvelope.phase === "decay") {
          const progress =
            stepperEnvelope.decayMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.decayMs, 1);
          stepperEnvelope.currentLevel =
            1 + (stepperEnvelope.sustainLevel - 1) * progress;
          if (progress >= 1) {
            stepperEnvelope.currentLevel = stepperEnvelope.sustainLevel;
            updateStepperEnvelopePhase(
              stepperEnvelope.noteHeld ? "sustain" : "release",
              now,
            );
          }
        } else if (stepperEnvelope.phase === "sustain") {
          stepperEnvelope.currentLevel = stepperEnvelope.sustainLevel;
          if (!stepperEnvelope.noteHeld) {
            updateStepperEnvelopePhase("release", now);
          }
        } else if (stepperEnvelope.phase === "release") {
          const progress =
            stepperEnvelope.releaseMs <= 0
              ? 1
              : Math.min(elapsed / stepperEnvelope.releaseMs, 1);
          stepperEnvelope.currentLevel =
            stepperEnvelope.phaseStartLevel * (1 - progress);
          if (progress >= 1) {
            stopStepperEnvelope();
            return;
          }
        }

        applyStepperEnvelopeOutput(true, STEPPER_ENVELOPE_SEND_DELAY_MS);
        updateStepperEnvelopeUi(now);
        stepperEnvelopeFrameId = window.requestAnimationFrame(tickStepperEnvelope);
      }

      function startStepperEnvelope(velocity = 127) {
        const now = performance.now();
        stepperEnvelope.originPercent = stepperBasePositionPercent;
        stepperEnvelope.velocityScale = Math.min(Math.max(velocity / 127, 0), 1);
        stepperEnvelope.noteHeld = true;
        if (!stepperEnvelope.active) {
          stepperEnvelope.active = true;
          stepperEnvelope.currentLevel = 0;
          stepperEnvelope.phaseStartLevel = 0;
        }
        updateStepperEnvelopePhase("attack", now);
        applyStepperEnvelopeOutput(canControlStepperPosition(), STEPPER_ENVELOPE_SEND_DELAY_MS);
        if (stepperEnvelopeFrameId === null) {
          stepperEnvelopeFrameId = window.requestAnimationFrame(
            tickStepperEnvelope,
          );
        }
        midiStatus.textContent =
          `Stepper 1 ADSR ${Math.round(stepperEnvelope.velocityScale * 100)}%`;
        return canControlStepperPosition();
      }

      function releaseStepperEnvelope() {
        if (!stepperEnvelope.active) {
          return;
        }

        stepperEnvelope.noteHeld = false;
        if (stepperEnvelope.phase !== "release") {
          updateStepperEnvelopePhase("release", performance.now());
        }
        midiStatus.textContent = "Stepper 1 Release";
      }

      function updateStepperUi() {
        const serialSupported = "serial" in navigator;
        stepperConnectButton.disabled =
          !serialSupported || stepperConnectInProgress;
        stepperHomeButton.disabled = stepperPort === null;
        stepperHomeButton.classList.toggle(
          "btn-danger-compact",
          stepperPort !== null && !stepperHomed,
        );
        stepperConnectButton.classList.toggle("connected", stepperPort !== null);
        stepperPositionSlider.disabled = !canControlStepperPosition();
        stepperCommandInput.disabled = !serialSupported || stepperPort === null;
        stepperCommandSendButton.disabled =
          !serialSupported || stepperPort === null;
        updateStepperHomedReadout();
        if (!serialSupported) {
          stepperStatus.textContent = "Web Serial unavailable in this browser";
          return;
        }

        stepperStatus.textContent = stepperStatusMessage;
      }

      function updateMidiUi() {
        const midiSupported = Boolean(navigator.requestMIDIAccess);
        midiConnectButton.disabled = !midiSupported;
        midiConnectButton.classList.toggle("connected", midiInputs.size > 0);
        if (!midiSupported) {
          midiStatus.textContent = "Web MIDI not supported";
        }
      }

      function formatSerialBytes(bytes) {
        return [...bytes]
          .map((value) => value.toString(16).padStart(2, "0"))
          .join(" ");
      }

      function formatLogTime(timestampMs) {
        const date = new Date(timestampMs);
        return [
          date.getHours().toString().padStart(2, "0"),
          date.getMinutes().toString().padStart(2, "0"),
          date.getSeconds().toString().padStart(2, "0"),
        ].join(":");
      }

      function formatLogPayload(payload) {
        if (typeof payload === "string") {
          const normalized = payload.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
          return normalized.length > 0 ? normalized : "(empty)";
        }

        if (payload instanceof Uint8Array || Array.isArray(payload)) {
          return formatSerialBytes(payload);
        }

        return String(payload);
      }

      function renderDeviceLog(role) {
        const stream = deviceLogStreams[role];
        const count = deviceLogCounts[role];
        const entries = deviceLogs[role];
        if (!stream || !count || !entries) {
          return;
        }

        count.textContent = `${entries.length} ${entries.length === 1 ? "entry" : "entries"}`;
        stream.replaceChildren();

        if (entries.length === 0) {
          const empty = document.createElement("div");
          empty.className = "log-empty";
          empty.textContent = `No ${role} traffic yet.`;
          stream.appendChild(empty);
          return;
        }

        for (const entry of entries) {
          const row = document.createElement("div");
          row.className = "log-entry";

          const time = document.createElement("span");
          time.className = "log-time";
          time.textContent = formatLogTime(entry.timestampMs);

          const direction = document.createElement("span");
          direction.className = `log-dir ${entry.direction}`;
          direction.textContent = entry.direction.toUpperCase();

          const payload = document.createElement("span");
          payload.className = "log-payload";
          payload.textContent = entry.payload;

          row.append(time, direction, payload);
          stream.appendChild(row);
        }

        stream.scrollTop = stream.scrollHeight;
      }

      function appendDeviceLog(role, direction, payload) {
        const entries = deviceLogs[role];
        if (!entries) {
          return;
        }

        entries.push({
          timestampMs: Date.now(),
          direction,
          payload: formatLogPayload(payload),
        });
        if (entries.length > DEVICE_LOG_LIMIT) {
          entries.splice(0, entries.length - DEVICE_LOG_LIMIT);
        }
        renderDeviceLog(role);
      }

      function clearDeviceLogs() {
        for (const role of Object.keys(deviceLogs)) {
          deviceLogs[role] = [];
          renderDeviceLog(role);
        }
      }

      function setLogPaneCollapsed(collapsed) {
        logPane.classList.toggle("collapsed", collapsed);
        logPaneToggleButton.setAttribute("aria-expanded", String(!collapsed));
        try {
          window.localStorage.setItem(LOG_PANE_COLLAPSED_KEY, collapsed ? "1" : "0");
        } catch {
          // Ignore storage failures; the pane can still be toggled.
        }
      }

      function logSerialTx(label, payload) {
        appendDeviceLog(label, "tx", payload);
        if (typeof payload === "string") {
          console.log(`[serial tx][${label}]`, JSON.stringify(payload));
          return;
        }

        console.log(`[serial tx][${label}]`, formatSerialBytes(payload));
      }

      function logSerialRx(label, payload) {
        appendDeviceLog(label, "rx", payload);
        if (typeof payload === "string") {
          console.log(`[serial rx][${label}]`, JSON.stringify(payload));
          return;
        }

        console.log(`[serial rx][${label}]`, formatSerialBytes(payload));
      }

      function syncStepperPositionFromBoard(percent) {
        const clampedPercent = Math.min(Math.max(percent, 0), 100);
        if (!stepperEnvelope.active) {
          stepperBasePositionPercent = clampedPercent;
        }
        applyStepperOutputPosition(clampedPercent, {
          send: false,
          save: false,
        });
        saveState();
      }

      function handleStepperLine(line) {
        const calibratedMatch = line.match(/^Calibrated:\s*(yes|no)$/i);
        if (calibratedMatch) {
          stepperHomed = calibratedMatch[1].toLowerCase() === "yes";
          if (!stepperHomed) {
            stepperTravelSteps = null;
            updateStepperTravelReadout();
          }
        }

        const travelMatch = line.match(/^Travel steps:\s*(\d+)$/i);
        if (travelMatch) {
          stepperTravelSteps = Number(travelMatch[1]);
          updateStepperTravelReadout();
          saveState();
        }

        const movedMatch = line.match(/^Moved to\s+([0-9]+(?:\.[0-9]+)?)%$/i);
        if (movedMatch) {
          syncStepperPositionFromBoard(Number(movedMatch[1]));
        }

        const currentPositionMatch = line.match(
          /^Current position:\s*([0-9]+(?:\.[0-9]+)?)%$/i,
        );
        if (currentPositionMatch) {
          syncStepperPositionFromBoard(Number(currentPositionMatch[1]));
        }

        stepperStatusMessage = line;
        updateStepperUi();
      }

      function setActiveFromFrame(frame) {
        activeNodes.clear();
        for (const nodeId of frame) {
          activeNodes.add(nodeId);
        }
      }

      function loadState() {
        try {
          const raw = window.localStorage.getItem(STORAGE_KEY);
          if (!raw) {
            return null;
          }

          const parsed = JSON.parse(raw);
          if (!parsed || typeof parsed !== "object") {
            return null;
          }

          const rings = Number(parsed.rings);
          const activeNodeIds = Array.isArray(parsed.activeNodeIds)
            ? parsed.activeNodeIds.filter((value) => typeof value === "string")
            : [];
          const knownNodeIds = Array.isArray(parsed.knownNodeIds)
            ? parsed.knownNodeIds.filter((value) => typeof value === "string")
            : [];
          const jetMode =
            typeof parsed.jetMode === "string" ? parsed.jetMode : "all";
          const labelMode =
            typeof parsed.labelMode === "string"
              ? parsed.labelMode
              : parsed.showLabels === true
                ? "address"
                : parsed.showDistanceLabels === true
                  ? "distance"
                  : parsed.showChannelLabels === true
                    ? "channel"
                    : "none";
          const animationLoopEnabled =
            typeof parsed.animationLoopEnabled === "boolean"
              ? parsed.animationLoopEnabled
              : true;
          const selectedSequenceId =
            typeof parsed.selectedSequenceId === "string"
              ? parsed.selectedSequenceId
              : "ripple";
          const animationSpeed = Number(parsed.animationSpeed);
          const stepperPositionPercent = Number(parsed.stepperPositionPercent);
          const stepperTravelSteps = Number(parsed.stepperTravelSteps);
          const stepperEnvelopeAttackMs = Number(parsed.stepperEnvelopeAttackMs);
          const stepperEnvelopeDecayMs = Number(parsed.stepperEnvelopeDecayMs);
          const stepperEnvelopeSustainLevel = Number(
            parsed.stepperEnvelopeSustainLevel,
          );
          const stepperEnvelopeReleaseMs = Number(
            parsed.stepperEnvelopeReleaseMs,
          );
          const savedCustomScripts =
            parsed.customScripts &&
            typeof parsed.customScripts === "object"
              ? parsed.customScripts
              : {};

          return {
            rings: Number.isFinite(rings) ? rings : null,
            activeNodeIds,
            knownNodeIds,
            jetMode,
            labelMode,
            animationLoopEnabled,
            selectedSequenceId,
            animationSpeed: Number.isFinite(animationSpeed)
              ? animationSpeed
              : 12,
            stepperPositionPercent: Number.isFinite(stepperPositionPercent)
              ? stepperPositionPercent
              : 50,
            stepperTravelSteps: Number.isFinite(stepperTravelSteps)
              ? stepperTravelSteps
              : null,
            stepperEnvelopeAttackMs: Number.isFinite(stepperEnvelopeAttackMs)
              ? stepperEnvelopeAttackMs
              : 180,
            stepperEnvelopeDecayMs: Number.isFinite(stepperEnvelopeDecayMs)
              ? stepperEnvelopeDecayMs
              : 220,
            stepperEnvelopeSustainLevel: Number.isFinite(stepperEnvelopeSustainLevel)
              ? stepperEnvelopeSustainLevel
              : 0.55,
            stepperEnvelopeReleaseMs: Number.isFinite(stepperEnvelopeReleaseMs)
              ? stepperEnvelopeReleaseMs
              : 320,
            customScripts: savedCustomScripts,
          };
        } catch {
          return null;
        }
      }

      function saveState() {
        try {
          window.localStorage.setItem(
            STORAGE_KEY,
            JSON.stringify({
              rings: Number(ringsInput.value),
              activeNodeIds: [...activeNodes],
              knownNodeIds: [...knownNodeIds],
              jetMode,
              labelMode,
              animationLoopEnabled,
              selectedSequenceId,
              animationSpeed,
              stepperPositionPercent: stepperBasePositionPercent,
              stepperTravelSteps,
              stepperEnvelopeAttackMs: stepperEnvelope.attackMs,
              stepperEnvelopeDecayMs: stepperEnvelope.decayMs,
              stepperEnvelopeSustainLevel: stepperEnvelope.sustainLevel,
              stepperEnvelopeReleaseMs: stepperEnvelope.releaseMs,
              customScripts:
                Object.keys(customScripts).length > 0
                  ? customScripts
                  : undefined,
            }),
          );
        } catch {
          // Ignore storage failures so interaction still works.
        }
      }

      function syncActiveNodes(nodes) {
        const validIds = new Set(nodes.map((node) => node.id));
        let changed = false;

        for (const nodeId of [...activeNodes]) {
          if (!validIds.has(nodeId)) {
            activeNodes.delete(nodeId);
            changed = true;
          }
        }

        for (const node of nodes) {
          if (!knownNodeIds.has(node.id)) {
            knownNodeIds.add(node.id);
            activeNodes.add(node.id);
            changed = true;
          }
        }

        if (changed) {
          saveState();
        }
      }

      function updateLabelModeSelect() {
        labelModeSelect.value = labelMode;
      }

      function findHitNode(clientX, clientY) {
        if (!scene) {
          return null;
        }

        const rect = canvas.getBoundingClientRect();
        const x = clientX - rect.left;
        const y = clientY - rect.top;
        let hit = null;
        let bestDistance = HIT_RADIUS;

        for (const node of scene.nodes) {
          const distance = Math.hypot(node.x - x, node.y - y);
          if (distance <= bestDistance) {
            bestDistance = distance;
            hit = node;
          }
        }

        return hit;
      }

      function updateStats() {
        if (!scene) {
          outlineStats.textContent = "Visible 0 / Total 0";
          spokeStats.textContent = "Visible 0 / Total 0";
          return;
        }

        const counts = {
          outline: { total: 0, visible: 0 },
          spoke: { total: 0, visible: 0 },
        };

        for (const node of scene.nodes) {
          if (node.type === "center") {
            counts.spoke.total += 1;
            if (activeNodes.has(node.id)) {
              counts.spoke.visible += 1;
            }
          }

          if (node.type === "vertex") {
            counts.outline.total += 1;
            if (activeNodes.has(node.id)) {
              counts.outline.visible += 1;
            }
          }
        }

        outlineStats.textContent = `Visible ${counts.outline.visible} / Total ${counts.outline.total}`;
        spokeStats.textContent = `Visible ${counts.spoke.visible} / Total ${counts.spoke.total}`;
      }

      function updateSpeedReadout() {
        speedReadout.value = `${Number(animationSpeed).toFixed(1)} steps/s`;
      }

      function updatePlayPauseButton() {
        playPauseButton.textContent =
          animationTimerId === null ? "Play" : "Pause";
      }

      function updateLoopToggleButton() {
        loopToggleButton.textContent = animationLoopEnabled
          ? "Loop On"
          : "Loop Off";
      }

      function getMappedRelayNodeIds(currentScene) {
        return getRelayMappedNodeIds(
          currentScene,
          buildDistanceMap(currentScene),
          angleFromCenter,
        );
      }

      function getRelayStateSnapshot(currentScene) {
        return buildRelayStates(
          currentScene,
          getMappedRelayNodeIds(currentScene),
          activeNodes,
        );
      }

      async function writeRelayFrame(frame) {
        if (!relayPort?.writable) {
          throw new Error("Relay port is not connected");
        }

        logSerialTx("relay", frame);
        const writer = relayPort.writable.getWriter();
        try {
          await writer.write(frame);
        } finally {
          writer.releaseLock();
        }
      }

      async function sendRelayCommand(command) {
        const frame = parseRelayCommandInput(command);
        if (!frame || frame.length === 0) {
          return;
        }

        await writeRelayFrame(frame);
        relayStatusMessage = "Relay command sent";
        updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
      }

      async function processRelaySyncQueue() {
        if (relaySyncInProgress || relayPort === null) {
          return;
        }

        relaySyncInProgress = true;

        try {
          while (relayStateQueue.length > 0 && relayPort !== null) {
            const nextStates = relayStateQueue.shift();
            relayLastStates = [...nextStates];
            await writeRelayFrame(buildRelayWriteMultipleFrame(nextStates));
            await new Promise((resolve) => setTimeout(resolve, 10));
          }

          if (relayPort !== null) {
            relayStatusMessage = "Relay sync connected";
          }
        } catch (error) {
          console.error(error);
          relayLastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
          relayStatusMessage = "Relay sync error";
        } finally {
          relaySyncInProgress = false;
          if (scene) {
            updateSerialUi(getMappedRelayNodeIds(scene).length);
          } else {
            updateSerialUi(0);
          }
        }
      }

      function syncRelayOutputs(currentScene) {
        const { mappedNodeIds, states } = getRelayStateSnapshot(currentScene);
        updateSerialUi(mappedNodeIds.length);

        if (relayPort === null) {
          return;
        }

        const queuedStates =
          relayStateQueue[relayStateQueue.length - 1] || relayLastStates;
        if (relayStatesEqual(queuedStates, states)) {
          return;
        }

        relayStateQueue.push([...states]);
        processRelaySyncQueue();
      }

      async function readRelayLoop(port) {
        while (relayPort === port && port.readable) {
          const reader = port.readable.getReader();
          relayReader = reader;
          try {
            while (relayPort === port) {
              const { value, done } = await reader.read();
              if (done) {
                break;
              }
              if (value?.length > 0) {
                logSerialRx("relay", value);
              }
            }
          } catch (error) {
            if (relayPort === port) {
              console.error(error);
              relayStatusMessage = "Relay read error";
              updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
            }
          } finally {
            relayReader = null;
            reader.releaseLock();
          }
        }
      }

      async function openRelayPort(port) {
        try {
          if (port === stepperPort) {
            relayStatusMessage = "Selected port is already in use by Stepper";
            updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
            return;
          }

          relayPort = port;
          await relayPort.open({
            baudRate: 115200,
            dataBits: 8,
            stopBits: 1,
            parity: "none",
            flowControl: "none",
          });
          rememberSerialPortRole("relay", port, RELAY_PORT_KEY, STEPPER_PORT_KEY);
          relayLastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
          relayStatusMessage = "Relay sync connected";
          appendDeviceLog("relay", "event", "connected");
          readRelayLoop(relayPort);
          if (scene) {
            syncRelayOutputs(scene);
          } else {
            updateSerialUi(0);
          }
        } catch (error) {
          relayPort = null;
          relayStatusMessage = "Relay connection failed";
          updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
          console.error(error);
        }
      }

      async function connectRelay() {
        if (!("serial" in navigator) || relayPort !== null) {
          return;
        }

        try {
          const ports = await navigator.serial.getPorts();
          const port =
            findPreferredSerialPort(ports, "relay", RELAY_PORT_KEY, STEPPER_PORT_KEY) ||
            (await navigator.serial.requestPort());
          await openRelayPort(port);
        } catch (error) {
          console.error(error);
        }
      }

      async function autoConnectRelay() {
        if (!("serial" in navigator) || relayPort !== null) {
          return;
        }

        try {
          const ports = await navigator.serial.getPorts();
          const port = findPreferredSerialPort(ports, "relay", RELAY_PORT_KEY, STEPPER_PORT_KEY);
          if (port) {
            await openRelayPort(port);
          }
        } catch (error) {
          console.error(error);
        }
      }

      async function disconnectRelay() {
        const wasConnected = relayPort !== null;
        relayStateQueue = [];
        relaySyncInProgress = false;
        if (relayReader !== null) {
          try {
            await relayReader.cancel();
          } catch (error) {
            console.error(error);
          }
        }

        if (relayPort !== null) {
          try {
            await relayPort.close();
          } catch (error) {
            console.error(error);
          }
        }

        relayPort = null;
        relayReader = null;
        relayLastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
        relayStatusMessage = "Relay sync disconnected";
        if (wasConnected) {
          appendDeviceLog("relay", "event", "disconnected");
        }
        updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
      }

      async function writeStepperCommand(command) {
        if (!stepperPort?.writable) {
          throw new Error("Stepper port is not connected");
        }

        logSerialTx("stepper", command);
        const writer = stepperPort.writable.getWriter();
        try {
          await writer.write(new TextEncoder().encode(`${command}\n`));
        } finally {
          writer.releaseLock();
        }
      }

      async function sendStepperCommand(command) {
        const trimmed = command.trim();
        if (!trimmed) {
          return;
        }

        await writeStepperCommand(trimmed);
        stepperStatusMessage = `Stepper command sent: ${trimmed}`;
        updateStepperUi();
      }

      async function requestStepperStatus() {
        if (stepperPort === null) {
          return;
        }

        await writeStepperCommand("status");
      }

      async function processStepperPositionQueue() {
        if (stepperPositionSendInProgress || !canControlStepperPosition()) {
          return;
        }

        stepperPositionSendInProgress = true;

        try {
          if (stepperQueuedPosition !== null && stepperPort !== null) {
            const nextPosition = stepperQueuedPosition;
            stepperQueuedPosition = null;
            await writeStepperCommand(`pos ${nextPosition.toFixed(1)}`);
            stepperLastSendAtMs = performance.now();
            stepperStatusMessage = `Stepper connected at ${nextPosition.toFixed(1)}%`;
            updateStepperUi();
          }
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper write failed";
          updateStepperUi();
        } finally {
          stepperPositionSendInProgress = false;
          if (stepperQueuedPosition !== null && stepperPort !== null) {
            scheduleStepperPositionSend();
          }
        }
      }

      function scheduleStepperPositionSend(delayMs = STEPPER_SEND_DELAY_MS) {
        if (stepperPositionSendTimerId !== null) {
          return;
        }

        stepperPositionSendTimerId = window.setTimeout(() => {
          stepperPositionSendTimerId = null;
          processStepperPositionQueue();
        }, delayMs);
      }

      function queueStepperPositionSend(delayMs = STEPPER_SEND_DELAY_MS) {
        if (!canControlStepperPosition()) {
          stepperQueuedPosition = null;
          return;
        }

        stepperQueuedPosition = stepperPositionPercent;

        if (stepperPositionSendInProgress) {
          return;
        }

        const elapsedMs = performance.now() - stepperLastSendAtMs;
        if (elapsedMs >= delayMs) {
          if (stepperPositionSendTimerId !== null) {
            window.clearTimeout(stepperPositionSendTimerId);
            stepperPositionSendTimerId = null;
          }
          processStepperPositionQueue();
          return;
        }

        scheduleStepperPositionSend(delayMs - elapsedMs);
      }

      async function flushStepperPositionSend() {
        if (stepperPositionSendTimerId !== null) {
          window.clearTimeout(stepperPositionSendTimerId);
          stepperPositionSendTimerId = null;
        }

        if (!canControlStepperPosition()) {
          stepperQueuedPosition = null;
          return;
        }

        stepperQueuedPosition = stepperPositionPercent;
        await processStepperPositionQueue();
      }

      async function homeStepper() {
        if (stepperPort === null) {
          return;
        }

        if (stepperPositionSendTimerId !== null) {
          window.clearTimeout(stepperPositionSendTimerId);
          stepperPositionSendTimerId = null;
        }
        stepperQueuedPosition = null;
        stepperHomed = false;
        stepperTravelSteps = null;
        updateStepperTravelReadout();
        stepperStatusMessage = "Homing at 400Hz";
        updateStepperUi();

        try {
          await writeStepperCommand("home 400");
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper home failed";
          updateStepperUi();
        }
      }

      async function readStepperLoop(port) {
        const decoder = new TextDecoder();
        let buffered = "";

        while (stepperPort === port && port.readable) {
          const reader = port.readable.getReader();
          stepperReader = reader;
          try {
            while (stepperPort === port) {
              const { value, done } = await reader.read();
              if (done) {
                break;
              }
              const chunk = decoder.decode(value, { stream: true });
              if (chunk.length > 0) {
                logSerialRx("stepper", chunk);
              }
              buffered += chunk;
              const lines = buffered.split(/\r?\n/);
              buffered = lines.pop() || "";
              for (const rawLine of lines) {
                const line = rawLine.trim();
                if (!line || line === ">" || line.startsWith(">")) {
                  continue;
                }
                handleStepperLine(line);
              }
            }
          } catch (error) {
            if (stepperPort === port) {
              console.error(error);
              stepperStatusMessage = "Stepper read error";
              updateStepperUi();
            }
          } finally {
            stepperReader = null;
            reader.releaseLock();
          }
          break;
        }
      }

      async function openStepperPort(port) {
        try {
          if (port === relayPort) {
            stepperStatusMessage = "Selected port is already in use by Relay";
            updateStepperUi();
            return;
          }

          stepperPort = port;
          await stepperPort.open({
            baudRate: STEPPER_SERIAL_BAUD,
            dataBits: 8,
            stopBits: 1,
            parity: "none",
            flowControl: "none",
          });
          rememberSerialPortRole("stepper", port, RELAY_PORT_KEY, STEPPER_PORT_KEY);
          stepperHomed = false;
          stepperTravelSteps = null;
          updateStepperTravelReadout();
          stepperStatusMessage = "Checking stepper status...";
          appendDeviceLog("stepper", "event", "connected");
          updateStepperUi();
          readStepperLoop(stepperPort);
          await requestStepperStatus();
        } catch (error) {
          stepperPort = null;
          stepperHomed = false;
          stepperStatusMessage =
            error?.name === "InvalidStateError"
              ? "Selected port is already open"
              : "Stepper connection failed";
          updateStepperUi();
          console.error(error);
        }
      }

      async function connectStepper() {
        if (
          !("serial" in navigator) ||
          stepperPort !== null ||
          stepperConnectInProgress
        ) {
          return;
        }

        stepperConnectInProgress = true;
        stepperStatusMessage = "Connecting stepper...";
        updateStepperUi();

        try {
          const ports = await navigator.serial.getPorts();
          const port =
            findPreferredSerialPort(ports, "stepper", RELAY_PORT_KEY, STEPPER_PORT_KEY) ||
            (await navigator.serial.requestPort());
          await openStepperPort(port);
        } catch (error) {
          stepperStatusMessage = "Stepper disconnected";
          updateStepperUi();
          console.error(error);
        } finally {
          stepperConnectInProgress = false;
          updateStepperUi();
        }
      }

      async function autoConnectStepper() {
        if (
          !("serial" in navigator) ||
          stepperPort !== null ||
          stepperConnectInProgress
        ) {
          return;
        }

        stepperConnectInProgress = true;
        stepperStatusMessage = "Connecting stepper...";
        updateStepperUi();

        try {
          const ports = await navigator.serial.getPorts();
          const port = findPreferredSerialPort(ports, "stepper", RELAY_PORT_KEY, STEPPER_PORT_KEY);
          if (port) {
            await openStepperPort(port);
          } else {
            stepperStatusMessage = "Stepper disconnected";
            updateStepperUi();
          }
        } catch (error) {
          stepperStatusMessage = "Stepper disconnected";
          updateStepperUi();
          console.error(error);
        } finally {
          stepperConnectInProgress = false;
          updateStepperUi();
        }
      }

      async function disconnectStepper() {
        const wasConnected = stepperPort !== null;
        if (stepperPositionSendTimerId !== null) {
          window.clearTimeout(stepperPositionSendTimerId);
          stepperPositionSendTimerId = null;
        }
        stepperQueuedPosition = null;
        stepperPositionSendInProgress = false;
        stepperLastSendAtMs = -Infinity;

        if (stepperReader !== null) {
          try {
            await stepperReader.cancel();
          } catch (error) {
            console.error(error);
          }
        }

        if (stepperPort !== null) {
          try {
            await stepperPort.close();
          } catch (error) {
            console.error(error);
          }
        }

        stepperPort = null;
        stepperReader = null;
        stepperHomed = false;
        stepperTravelSteps = null;
        updateStepperTravelReadout();
        stepperStatusMessage = "Stepper disconnected";
        if (wasConnected) {
          appendDeviceLog("stepper", "event", "disconnected");
        }
        updateStepperUi();
      }

      function buildPath(currentScene, targetId) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode || centerNode.id === targetId) {
          return centerNode ? [centerNode.id] : [];
        }

        const visited = new Set([centerNode.id]);
        const queue = [centerNode.id];
        const parentMap = new Map();

        while (queue.length > 0) {
          const nodeId = queue.shift();
          const node = currentScene.nodes.find(
            (candidate) => candidate.id === nodeId,
          );
          for (const neighborId of node.neighbors) {
            if (visited.has(neighborId)) {
              continue;
            }
            visited.add(neighborId);
            parentMap.set(neighborId, nodeId);
            if (neighborId === targetId) {
              const path = [targetId];
              let currentId = targetId;
              while (parentMap.has(currentId)) {
                currentId = parentMap.get(currentId);
                path.push(currentId);
              }
              return path.reverse();
            }
            queue.push(neighborId);
          }
        }

        return [centerNode.id];
      }

      function buildConstrainedPath(
        currentScene,
        startId,
        targetId,
        allowedIds,
      ) {
        if (startId === targetId) {
          return [startId];
        }

        const nodeMap = new Map(
          currentScene.nodes.map((node) => [node.id, node]),
        );
        const visited = new Set([startId]);
        const queue = [startId];
        const parentMap = new Map();

        while (queue.length > 0) {
          const nodeId = queue.shift();
          const node = nodeMap.get(nodeId);
          for (const neighborId of node.neighbors) {
            if (!allowedIds.has(neighborId) || visited.has(neighborId)) {
              continue;
            }

            visited.add(neighborId);
            parentMap.set(neighborId, nodeId);
            if (neighborId === targetId) {
              const path = [targetId];
              let currentId = targetId;
              while (parentMap.has(currentId)) {
                currentId = parentMap.get(currentId);
                path.push(currentId);
              }
              return path.reverse();
            }

            queue.push(neighborId);
          }
        }

        return [];
      }

      function buildWalkFrames(pathIds) {
        if (pathIds.length === 0) {
          return [];
        }

        const frames = [new Set([pathIds[0]])];

        for (let index = 1; index < pathIds.length; index += 1) {
          frames.push(new Set([pathIds[index - 1], pathIds[index]]));
          frames.push(new Set([pathIds[index]]));
        }

        return dedupeFrames(frames);
      }

      // ── Animation Scripting Language ──────────────────────────
      //
      // Line-oriented language for programming hex grid animations.
      // Designed to be interpretable in both JavaScript and Arduino.
      //
      // Node selectors (used as arguments):
      //   all            – every node in the scene
      //   center         – the origin node (c:0,0)
      //   type:center    – all center (spoke) nodes
      //   type:vertex    – all vertex (outline) nodes
      //   dist:N         – nodes at BFS distance N
      //   dist:N..M      – nodes at BFS distance N through M
      //
      // Variables:
      //   $d       – current distance in a for-dist loop
      //   $max     – maximum distance in the scene
      //   $d-1,$d+1,$max-1 – arithmetic on variables
      //   $n       – current node in a for-node loop
      //
      // Frame commands:
      //   clear             – clear the active set
      //   add <sel>         – add matching nodes to active set
      //   remove <sel>      – remove matching nodes from active set
      //   set <sel>         – clear then add (shorthand)
      //   frame             – emit current active set as a frame
      //
      // Loops:
      //   for-dist [from] [to]       – iterate $d ascending
      //   for-dist-rev [from] [to]   – iterate $d descending
      //   for-node <sel> [sort angle|shell-angle] – iterate $n
      //   end                        – end the innermost loop
      //
      // Walk commands (stateful cursor):
      //   cursor <sel>                     – set cursor position
      //   walk-to $n [constrain <sel>]     – BFS to $n, emit walk frames
      //   dfs [sort shell-angle]           – DFS from cursor, emit walk frames
      //
      // Scene filter:
      //   filter-scene type:vertex    – keep only vertex nodes + center
      //

      const builtinScripts = {
        ripple: `# Ripple: expanding wave then contracting
for-dist
  add dist:$d
  frame
end
for-dist-rev
  remove dist:$d
  frame
end`,

        band: `# Band: traveling pair of adjacent layers
set dist:0
frame
for-dist 1
  set dist:$d-1..$d
  frame
  set dist:$d
  frame
end
for-dist-rev $max-1 0
  set dist:$d..$d+1
  frame
  set dist:$d
  frame
end`,

        chase: `# Chase: walk around each distance ring
cursor center
for-dist 1
  for-node dist:$d sort angle
    walk-to $n constrain dist:$d-1..$d
  end
end
walk-to center`,

        walker: `# Walker: depth-first graph traversal
cursor center
dfs sort shell-angle`,

        "ripple-outline": `# Ripple (outlines only)
filter-scene type:vertex
for-dist
  add dist:$d
  frame
end
for-dist-rev
  remove dist:$d
  frame
end`,

        "band-outline": `# Band (outlines only)
filter-scene type:vertex
set dist:0
frame
for-dist 1
  set dist:$d-1..$d
  frame
  set dist:$d
  frame
end
for-dist-rev $max-1 0
  set dist:$d..$d+1
  frame
  set dist:$d
  frame
end`,

        "chase-outline": `# Chase (outlines only)
filter-scene type:vertex
cursor center
for-dist 1
  for-node dist:$d sort angle
    walk-to $n constrain dist:$d-1..$d
  end
end
walk-to center`,

        "walker-outline": `# Walker (outlines only)
filter-scene type:vertex
cursor center
dfs sort shell-angle`,
      };

      function resolveSelector(token, vars, distanceMap, currentScene) {
        if (token === "$n") {
          return vars.n ? [vars.n] : [];
        }
        if (token === "all") {
          return currentScene.nodes.map((n) => n.id);
        }
        if (token === "center") {
          const c = getCenterNode(currentScene);
          return c ? [c.id] : [];
        }
        if (token === "type:center") {
          return currentScene.nodes
            .filter((n) => n.type === "center")
            .map((n) => n.id);
        }
        if (token === "type:vertex") {
          return currentScene.nodes
            .filter((n) => n.type === "vertex")
            .map((n) => n.id);
        }

        const distMatch = token.match(
          /^dist:(.+?)(?:\.\.(.+))?$/,
        );
        if (distMatch) {
          const lo = resolveNum(distMatch[1], vars);
          const hi = distMatch[2] !== undefined
            ? resolveNum(distMatch[2], vars)
            : lo;
          const ids = [];
          for (const node of currentScene.nodes) {
            const d = distanceMap.get(node.id) ?? 0;
            if (d >= lo && d <= hi) {
              ids.push(node.id);
            }
          }
          return ids;
        }
        throw new Error(`Unknown selector: ${token}`);
      }

      function resolveNum(expr, vars) {
        const s = expr.trim();
        if (/^-?\d+$/.test(s)) return Number(s);
        if (s === "$d") return vars.d ?? 0;
        if (s === "$max") return vars.max ?? 0;
        if (s === "$n") return vars.n ?? "";
        const varOp = s.match(/^\$(\w+)([+-])(\d+)$/);
        if (varOp) {
          const base = varOp[1] === "d" ? (vars.d ?? 0) : varOp[1] === "max" ? (vars.max ?? 0) : 0;
          const offset = Number(varOp[3]);
          return varOp[2] === "+" ? base + offset : Math.max(0, base - offset);
        }
        throw new Error(`Cannot resolve number: ${expr}`);
      }

      function executeAnimationScript(scriptText, inputScene) {
        const lines = scriptText
          .split("\n")
          .map((line) => line.replace(/#.*$/, "").trim())
          .filter((line) => line.length > 0);

        let currentScene = inputScene;
        const distanceMap = buildDistanceMap(currentScene);
        let maxDist = 0;
        for (const node of currentScene.nodes) {
          const d = distanceMap.get(node.id) ?? 0;
          if (d > maxDist) maxDist = d;
        }

        const active = new Set();
        const frames = [];
        const vars = { d: 0, max: maxDist };

        function resolveAll(tokens) {
          const ids = [];
          for (const tok of tokens) {
            ids.push(...resolveSelector(tok, vars, distanceMap, currentScene));
          }
          return ids;
        }

        // Pre-scan for loops to find matching end indices
        const endMap = new Map(); // startIndex -> endIndex
        const startMap = new Map(); // endIndex -> startIndex
        const stack = [];
        for (let i = 0; i < lines.length; i++) {
          const cmd = lines[i].split(/\s+/)[0];
          if (cmd === "for-dist" || cmd === "for-dist-rev" || cmd === "for-node") {
            stack.push(i);
          } else if (cmd === "end") {
            if (stack.length === 0) throw new Error("Unexpected 'end'");
            const start = stack.pop();
            endMap.set(start, i);
            startMap.set(i, start);
          }
        }
        if (stack.length > 0) throw new Error("Missing 'end'");

        let pc = 0;
        const loopStack = [];
        let iterations = 0;
        const MAX_ITERATIONS = 100000;

        while (pc < lines.length) {
          if (++iterations > MAX_ITERATIONS) {
            throw new Error("Animation script exceeded maximum iterations");
          }

          const tokens = lines[pc].split(/\s+/);
          const cmd = tokens[0];

          switch (cmd) {
            case "clear":
              active.clear();
              break;

            case "add":
              for (const id of resolveAll(tokens.slice(1))) {
                active.add(id);
              }
              break;

            case "remove":
              for (const id of resolveAll(tokens.slice(1))) {
                active.delete(id);
              }
              break;

            case "set":
              active.clear();
              for (const id of resolveAll(tokens.slice(1))) {
                active.add(id);
              }
              break;

            case "frame":
              frames.push(new Set(active));
              break;

            case "for-dist":
            case "for-dist-rev": {
              const from =
                tokens.length > 1 ? resolveNum(tokens[1], vars) : (cmd === "for-dist-rev" ? maxDist : 0);
              const to =
                tokens.length > 2 ? resolveNum(tokens[2], vars) : (cmd === "for-dist-rev" ? 1 : maxDist);
              const step = cmd === "for-dist-rev" ? -1 : 1;
              const endPc = endMap.get(pc);
              vars.d = from;
              if ((step > 0 && from > to) || (step < 0 && from < to)) {
                // Empty loop
                pc = endPc;
                break;
              }
              loopStack.push({ startPc: pc, endPc, from, to, step });
              break;
            }

            case "end": {
              const loop = loopStack[loopStack.length - 1];
              if (!loop) throw new Error("Unexpected 'end'");
              if (loop.type === "node") {
                // If walk failed, try next candidate without removing
                if (vars._walkSuccess === false) {
                  loop.nodeIndex++;
                  if (loop.nodeIndex >= loop.nodeIds.length) {
                    loopStack.pop(); // All candidates exhausted
                  } else {
                    vars.n = loop.nodeIds[loop.nodeIndex];
                    vars._walkSuccess = true;
                    pc = loop.startPc;
                  }
                  break;
                }
                // Remove visited node and re-sort remaining from cursor
                loop.nodeIds.splice(loop.nodeIndex, 1);
                if (loop.nodeIds.length === 0) {
                  loopStack.pop();
                } else {
                  if (loop.sortMode !== "shell-angle") {
                    const nm = new Map(
                      currentScene.nodes.map((n) => [n.id, n]),
                    );
                    const curNode = vars.cursor
                      ? nm.get(vars.cursor)
                      : null;
                    const base = curNode
                      ? angleFromCenter(curNode)
                      : 0;
                    loop.nodeIds.sort((a, b) => {
                      const na = nm.get(a);
                      const nb = nm.get(b);
                      if (!na || !nb) return 0;
                      return (
                        positiveAngleDelta(base, angleFromCenter(na)) -
                        positiveAngleDelta(base, angleFromCenter(nb))
                      );
                    });
                  }
                  loop.nodeIndex = 0;
                  vars.n = loop.nodeIds[0];
                  vars._walkSuccess = true;
                  pc = loop.startPc;
                }
              } else {
                vars.d = vars.d + loop.step;
                const done =
                  loop.step > 0 ? vars.d > loop.to : vars.d < loop.to;
                if (done) {
                  loopStack.pop();
                } else {
                  pc = loop.startPc;
                }
              }
              break;
            }

            case "filter-scene": {
              if (tokens[1] === "type:vertex") {
                currentScene = buildOutlineOnlyScene(currentScene);
              } else {
                throw new Error(`Unknown filter: ${tokens[1]}`);
              }
              break;
            }

            case "for-node": {
              // for-node <selector> [sort angle|shell-angle]
              const endPc = endMap.get(pc);
              const sortIdx = tokens.indexOf("sort");
              const selTokens =
                sortIdx > 0 ? tokens.slice(1, sortIdx) : tokens.slice(1);
              const sortMode = sortIdx > 0 ? tokens[sortIdx + 1] : "angle";
              let nodeIds = resolveAll(selTokens);

              // Sort nodes
              const nodeMap = new Map(
                currentScene.nodes.map((n) => [n.id, n]),
              );
              const resolved = nodeIds
                .map((id) => nodeMap.get(id))
                .filter(Boolean);
              if (sortMode === "shell-angle") {
                resolved.sort((a, b) => {
                  const sd = a.shell - b.shell;
                  return sd !== 0 ? sd : angleFromCenter(a) - angleFromCenter(b);
                });
              } else {
                // Sort by angle relative to cursor position
                const cursorNode = vars.cursor
                  ? nodeMap.get(vars.cursor)
                  : null;
                const baseAngle = cursorNode
                  ? angleFromCenter(cursorNode)
                  : 0;
                resolved.sort(
                  (a, b) =>
                    positiveAngleDelta(baseAngle, angleFromCenter(a)) -
                    positiveAngleDelta(baseAngle, angleFromCenter(b)),
                );
              }
              nodeIds = resolved.map((n) => n.id);

              if (nodeIds.length === 0) {
                pc = endPc;
                break;
              }
              vars.n = nodeIds[0];
              vars._walkSuccess = true;
              loopStack.push({
                type: "node",
                sortMode,
                startPc: pc,
                endPc,
                nodeIds,
                nodeIndex: 0,
              });
              break;
            }

            case "cursor": {
              if (tokens[1] === "center") {
                const c = getCenterNode(currentScene);
                vars.cursor = c ? c.id : null;
              } else if (tokens[1] === "$n") {
                vars.cursor = vars.n || null;
              } else {
                const ids = resolveAll(tokens.slice(1));
                vars.cursor = ids.length > 0 ? ids[0] : null;
              }
              break;
            }

            case "walk-to": {
              // walk-to $n [constrain <selector>]
              const targetId =
                tokens[1] === "$n" ? vars.n : resolveAll([tokens[1]])[0];
              if (!targetId || !vars.cursor) break;

              const constrainIdx = tokens.indexOf("constrain");
              let path;
              if (constrainIdx > 0) {
                const allowedIds = new Set(
                  resolveAll(tokens.slice(constrainIdx + 1)),
                );
                allowedIds.add(vars.cursor);
                // From center use unconstrained BFS (matches native chase)
                const centerNode = getCenterNode(currentScene);
                if (centerNode && vars.cursor === centerNode.id) {
                  path = buildPath(currentScene, targetId);
                } else {
                  path = buildConstrainedPath(
                    currentScene,
                    vars.cursor,
                    targetId,
                    allowedIds,
                  );
                }
              } else {
                const allIds = new Set(
                  currentScene.nodes.map((n) => n.id),
                );
                path = buildConstrainedPath(
                  currentScene,
                  vars.cursor,
                  targetId,
                  allIds,
                );
              }

              if (path.length > 0) {
                for (let i = 1; i < path.length; i++) {
                  frames.push(new Set([path[i - 1], path[i]]));
                }
                vars.cursor = targetId;
                vars._walkSuccess = true;
              } else {
                vars._walkSuccess = false;
              }
              break;
            }

            case "dfs": {
              // dfs [sort shell-angle]
              const centerNode = getCenterNode(currentScene);
              if (!centerNode) break;
              const startId = vars.cursor || centerNode.id;
              const nodeMap = new Map(
                currentScene.nodes.map((n) => [n.id, n]),
              );
              const visited = new Set([startId]);
              const walk = [startId];

              function dfsVisit(nodeId) {
                const node = nodeMap.get(nodeId);
                if (!node) return;
                const neighbors = [...node.neighbors]
                  .map((id) => nodeMap.get(id))
                  .filter(Boolean)
                  .sort((a, b) => {
                    const sd = a.shell - b.shell;
                    return sd !== 0
                      ? sd
                      : angleFromCenter(a) - angleFromCenter(b);
                  });
                for (const neighbor of neighbors) {
                  if (visited.has(neighbor.id)) continue;
                  visited.add(neighbor.id);
                  walk.push(neighbor.id);
                  dfsVisit(neighbor.id);
                  walk.push(nodeId);
                }
              }

              dfsVisit(startId);
              frames.push(...buildWalkFrames(walk));
              break;
            }

            default:
              throw new Error(`Unknown command: ${cmd}`);
          }
          pc++;
        }

        return dedupeFrames(frames);
      }

      function buildOutlineOnlyScene(currentScene) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode) {
          return currentScene;
        }

        const allowedIds = new Set(
          currentScene.nodes
            .filter(
              (node) => node.type === "vertex" || node.id === centerNode.id,
            )
            .map((node) => node.id),
        );

        return {
          ...currentScene,
          nodes: currentScene.nodes
            .filter((node) => allowedIds.has(node.id))
            .map((node) => ({
              ...node,
              neighbors: new Set(
                [...node.neighbors].filter((neighborId) =>
                  allowedIds.has(neighborId),
                ),
              ),
            })),
        };
      }

      function buildRippleFrames(currentScene) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode) {
          return [];
        }

        const distanceMap = buildDistanceMap(currentScene);
        const layers = new Map();
        for (const node of currentScene.nodes) {
          const distance = distanceMap.get(node.id) ?? 0;
          if (!layers.has(distance)) {
            layers.set(distance, []);
          }
          layers.get(distance).push(node.id);
        }

        const orderedDistances = [...layers.keys()].sort(
          (left, right) => left - right,
        );
        const frames = [];
        const active = new Set();

        for (const distance of orderedDistances) {
          for (const nodeId of layers.get(distance)) {
            active.add(nodeId);
          }
          frames.push(new Set(active));
        }

        for (let index = orderedDistances.length - 1; index > 0; index -= 1) {
          for (const nodeId of layers.get(orderedDistances[index])) {
            active.delete(nodeId);
          }
          frames.push(new Set(active));
        }

        return dedupeFrames(frames);
      }

      function buildBandFrames(currentScene) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode) {
          return [];
        }

        const distanceMap = buildDistanceMap(currentScene);
        const layers = new Map();
        for (const node of currentScene.nodes) {
          const distance = distanceMap.get(node.id) ?? 0;
          if (!layers.has(distance)) {
            layers.set(distance, []);
          }
          layers.get(distance).push(node.id);
        }

        const orderedDistances = [...layers.keys()].sort(
          (left, right) => left - right,
        );
        const frames = [new Set([centerNode.id])];

        for (let index = 1; index < orderedDistances.length; index += 1) {
          const previous = layers.get(orderedDistances[index - 1]);
          const current = layers.get(orderedDistances[index]);
          frames.push(new Set([...previous, ...current]));
          frames.push(new Set(current));
        }

        for (let index = orderedDistances.length - 2; index >= 0; index -= 1) {
          const current = layers.get(orderedDistances[index]);
          const next = layers.get(orderedDistances[index + 1]);
          frames.push(new Set([...current, ...next]));
          frames.push(new Set(current));
        }

        return dedupeFrames(frames);
      }

      function buildPerimeterChaseFrames(currentScene) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode) {
          return [];
        }

        const distanceMap = buildDistanceMap(currentScene);
        const layers = new Map();

        for (const node of currentScene.nodes) {
          const distance = distanceMap.get(node.id) ?? 0;
          if (!layers.has(distance)) {
            layers.set(distance, []);
          }
          layers.get(distance).push(node);
        }

        const orderedDistances = [...layers.keys()]
          .filter((distance) => distance > 0)
          .sort((left, right) => left - right);

        if (orderedDistances.length === 0) {
          return buildRippleFrames(currentScene);
        }

        const frames = [];
        let currentId = centerNode.id;

        for (const distance of orderedDistances) {
          const layerNodes = sortByAngle(layers.get(distance));
          const remaining = new Set(layerNodes.map((node) => node.id));

          while (remaining.size > 0) {
            const currentNode = currentScene.nodes.find(
              (node) => node.id === currentId,
            );
            const currentAngle = currentNode ? angleFromCenter(currentNode) : 0;
            const orderedTargets = [...remaining]
              .map((nodeId) =>
                currentScene.nodes.find((node) => node.id === nodeId),
              )
              .sort((left, right) => {
                return (
                  positiveAngleDelta(currentAngle, angleFromCenter(left)) -
                  positiveAngleDelta(currentAngle, angleFromCenter(right))
                );
              });

            let chosenPath = [];
            let chosenTargetId = null;

            for (const target of orderedTargets) {
              const allowedIds = new Set(
                currentScene.nodes
                  .filter((node) => {
                    const nodeDistance = distanceMap.get(node.id) ?? 0;
                    return (
                      nodeDistance === distance || nodeDistance === distance - 1
                    );
                  })
                  .map((node) => node.id),
              );
              allowedIds.add(currentId);
              const path =
                distance === orderedDistances[0] && currentId === centerNode.id
                  ? buildPath(currentScene, target.id)
                  : buildConstrainedPath(
                      currentScene,
                      currentId,
                      target.id,
                      allowedIds,
                    );

              if (path.length > 0) {
                chosenPath = path;
                chosenTargetId = target.id;
                break;
              }
            }

            if (chosenPath.length === 0 || chosenTargetId === null) {
              return buildGraphWalkerFrames(currentScene);
            }

            if (frames.length === 0) {
              frames.push(new Set([chosenPath[0]]));
            }

            for (let index = 1; index < chosenPath.length; index += 1) {
              frames.push(new Set([chosenPath[index - 1], chosenPath[index]]));
              frames.push(new Set([chosenPath[index]]));
            }

            remaining.delete(chosenTargetId);
            currentId = chosenTargetId;
          }
        }

        return dedupeFrames(frames);
      }

      function buildGraphWalkerFrames(currentScene) {
        const centerNode = getCenterNode(currentScene);
        if (!centerNode) {
          return [];
        }

        const nodeMap = new Map(
          currentScene.nodes.map((node) => [node.id, node]),
        );
        const visited = new Set([centerNode.id]);
        const walk = [centerNode.id];

        function visit(nodeId) {
          const node = nodeMap.get(nodeId);
          const orderedNeighbors = [...node.neighbors]
            .map((neighborId) => nodeMap.get(neighborId))
            .sort((left, right) => {
              const shellDelta = left.shell - right.shell;
              if (shellDelta !== 0) {
                return shellDelta;
              }
              return angleFromCenter(left) - angleFromCenter(right);
            });

          for (const neighbor of orderedNeighbors) {
            if (visited.has(neighbor.id)) {
              continue;
            }
            visited.add(neighbor.id);
            walk.push(neighbor.id);
            visit(neighbor.id);
            walk.push(nodeId);
          }
        }

        visit(centerNode.id);
        return buildWalkFrames(walk);
      }

      // ── MIDI Controller (M-VAVE SMC-PAD) ────────────────────
      //
      // Pads: Note On/Off, default notes 36-47 (C2-B2).
      //   Note 36 triggers Stepper 1 ADSR, other mapped pads momentarily
      //   trigger relay channels.
      // Transport: CC 115=prev, CC 116=next, CC 117=stop, CC 118=play
      // Speed knob: auto-detect first CC in 1-8 or 70-77 range.

      function handleMidiMessage(event) {
        const [status, data1, data2] = event.data;
        const type = status & 0xf0;
        appendDeviceLog("midi", "rx", formatMidiMessage(event.data, event.target));

        // Note On
        if (type === 0x90 && data2 > 0) {
          if (data1 === MIDI_NOTE_STEPPER_1_ENV) {
            const sendsToStepper = startStepperEnvelope(data2);
            midiStatus.textContent = sendsToStepper
              ? `Stepper 1 ADSR on (note ${data1}, vel ${data2})`
              : `Stepper 1 ADSR visual only (note ${data1}, vel ${data2})`;
            return;
          }
          if (data1 === MIDI_NOTE_ALL_OFF && scene) {
            stopAnimation();
            activeNodes.clear();
            saveState();
            render();
            midiStatus.textContent = "All Off";
            return;
          }
          if (data1 === MIDI_NOTE_ALL_ON && scene) {
            stopAnimation();
            for (const node of scene.nodes) activeNodes.add(node.id);
            saveState();
            render();
            midiStatus.textContent = "All On";
            return;
          }
          const chIndex = MIDI_PAD_MAP.get(data1);
          if (chIndex !== undefined && scene) {
            const mappedIds = getMappedRelayNodeIds(scene);
            if (chIndex < mappedIds.length) {
              midiHeldNotes.add(data1);
              stopAnimation();
              activeNodes.add(mappedIds[chIndex]);
              saveState();
              render();
            }
          }
          midiStatus.textContent =
            chIndex !== undefined
              ? `Ch ${chIndex + 1} on (note ${data1})`
              : `Note ${data1} vel ${data2}`;
          return;
        }

        // Note Off
        if (type === 0x80 || (type === 0x90 && data2 === 0)) {
          if (data1 === MIDI_NOTE_STEPPER_1_ENV) {
            releaseStepperEnvelope();
            return;
          }
          const chIndex = MIDI_PAD_MAP.get(data1);
          if (chIndex !== undefined && scene) {
            const mappedIds = getMappedRelayNodeIds(scene);
            if (chIndex < mappedIds.length) {
              midiHeldNotes.delete(data1);
              activeNodes.delete(mappedIds[chIndex]);
              saveState();
              render();
            }
          }
          return;
        }

        // Control Change
        if (type === 0xb0) {
          if (data1 === MIDI_CC_STEPPER_1_ATTACK) {
            stepperEnvelope.attackMs = 20 + (data2 / 127) * 1980;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Stepper 1 Attack: ${Math.round(stepperEnvelope.attackMs)}ms`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_DECAY) {
            stepperEnvelope.decayMs = 20 + (data2 / 127) * 1980;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Stepper 1 Decay: ${Math.round(stepperEnvelope.decayMs)}ms`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_SUSTAIN) {
            stepperEnvelope.sustainLevel = data2 / 127;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Stepper 1 Sustain: ${Math.round(stepperEnvelope.sustainLevel * 100)}%`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_RELEASE) {
            stepperEnvelope.releaseMs = 20 + (data2 / 127) * 1980;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Stepper 1 Release: ${Math.round(stepperEnvelope.releaseMs)}ms`;
            return;
          }

          // Speed knob
          if (data1 === MIDI_CC_SPEED) {
            animationSpeed = 1 + (data2 / 127) * 17;
            speedSlider.value = String(animationSpeed);
            updateSpeedReadout();
            saveState();
            midiStatus.textContent =
              `Speed: ${animationSpeed.toFixed(1)} steps/s`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1) {
            if (canControlStepperPosition()) {
              setStepperBasePosition((data2 / 127) * 100);
              midiStatus.textContent =
                `Stepper 1: ${stepperBasePositionPercent.toFixed(1)}% (CC ${data1})`;
            } else {
              midiStatus.textContent = "Stepper 1 not homed";
            }
            return;
          }

          // Transport (trigger on value > 0)
          if (data2 > 0) {
            if (data1 === MIDI_CC_PLAY) {
              if (animationTimerId === null) startAnimation();
              midiStatus.textContent = "Play";
              return;
            }
            if (data1 === MIDI_CC_PAUSE) {
              stopAnimation();
              midiStatus.textContent = "Pause";
              return;
            }
            if (data1 === MIDI_CC_NEXT) {
              const idx = sequenceSelect.selectedIndex;
              sequenceSelect.selectedIndex =
                (idx + 1) % sequenceSelect.options.length;
              selectedSequenceId = sequenceSelect.value;
              updateEditorFromSequence();
              saveState();
              if (animationTimerId !== null) startAnimation();
              midiStatus.textContent = `Next: ${selectedSequenceId}`;
              return;
            }
            if (data1 === MIDI_CC_BACK) {
              const idx = sequenceSelect.selectedIndex;
              sequenceSelect.selectedIndex =
                (idx - 1 + sequenceSelect.options.length) %
                sequenceSelect.options.length;
              selectedSequenceId = sequenceSelect.value;
              updateEditorFromSequence();
              saveState();
              if (animationTimerId !== null) startAnimation();
              midiStatus.textContent = `Prev: ${selectedSequenceId}`;
              return;
            }
          }

          midiStatus.textContent = `CC ${data1} = ${data2}`;
          return;
        }

        midiStatus.textContent =
          `MIDI: ${[...event.data].map((b) => b.toString(16).padStart(2, "0")).join(" ")}`;
      }

      function connectAllMidiInputs() {
        if (!midiAccess) {
          return;
        }

        const result = connectMidiAccessInputs(
          midiAccess,
          midiInputs,
          handleMidiMessage,
        );
        appendDeviceLog(
          "midi",
          "event",
          result.availableInputs.length > 0
            ? `available inputs: ${result.availableInputs.join(", ")}`
            : "available inputs: none",
        );
        for (const label of result.connectedLabels) {
          appendDeviceLog("midi", "event", `connected: ${label}`);
          console.log("MIDI: connected to", label);
        }
        for (const label of result.disconnectedLabels) {
          appendDeviceLog("midi", "event", `disconnected: ${label}`);
        }

        if (result.connectableCount === 0) {
          midiStatus.textContent = "No MIDI devices found";
        } else if (midiInputs.size > 1) {
          midiStatus.textContent = `Connected: ${midiInputs.size} MIDI inputs`;
        } else if (midiInputs.size === 1) {
          midiStatus.textContent =
            `Connected: ${getMidiInputLabel(midiInputs.values().next().value)}`;
        }
        updateMidiUi();
      }

      function disconnectMidi() {
        const hadInputs = disconnectMidiInputs(midiInputs);
        midiStatus.textContent = "Disconnected";
        if (hadInputs) {
          appendDeviceLog("midi", "event", "disconnected");
        }
        updateMidiUi();
      }

      async function initMidi() {
        if (!navigator.requestMIDIAccess) {
          midiStatus.textContent = "Web MIDI not supported";
          updateMidiUi();
          return;
        }
        try {
          midiAccess = await navigator.requestMIDIAccess({ sysex: false });
        } catch (err) {
          midiStatus.textContent = "MIDI access denied";
          updateMidiUi();
          console.error("MIDI:", err);
          return;
        }

        connectAllMidiInputs();
        midiAccess.onstatechange = connectAllMidiInputs;
      }

      function stopAnimation() {
        if (animationTimerId !== null) {
          window.clearTimeout(animationTimerId);
          animationTimerId = null;
          updatePlayPauseButton();
        }
      }

      function scheduleNextAnimationFrame() {
        if (animationFrameIndex >= animationFrames.length) {
          if (!animationLoopEnabled || animationFrames.length === 0) {
            stopAnimation();
            return;
          }

          animationFrameIndex = 0;
        }

        setActiveFromFrame(animationFrames[animationFrameIndex]);
        animationFrameIndex += 1;
        saveState();
        render();

        animationTimerId = window.setTimeout(
          scheduleNextAnimationFrame,
          Math.max(80, Math.round(1000 / animationSpeed)),
        );
        updatePlayPauseButton();
      }

      function getScriptForSequence(seqId) {
        if (customScripts[seqId] !== undefined) {
          return customScripts[seqId];
        }
        return builtinScripts[seqId] || builtinScripts.ripple;
      }

      function startAnimation() {
        stopAnimation();
        scene = buildScene(
          Number(ringsInput.value),
          canvas.clientWidth,
          canvas.clientHeight,
          jetMode,
        );
        syncActiveNodes(scene.nodes);
        const fullScene = {
          ...scene,
          nodes: scene.allNodes,
        };
        const visibleIds = new Set(scene.nodes.map((n) => n.id));
        try {
          const script = getScriptForSequence(selectedSequenceId);
          const rawFrames = executeAnimationScript(script, fullScene);
          animationFrames = dedupeFrames(
            rawFrames.map(
              (frame) =>
                new Set([...frame].filter((id) => visibleIds.has(id))),
            ),
          );
          editorError.style.display = "none";
        } catch (err) {
          editorError.textContent = err.message;
          editorError.style.display = "block";
          return;
        }
        animationFrameIndex = 0;
        if (animationFrames.length === 0) {
          return;
        }
        scheduleNextAnimationFrame();
      }

      function resetVisualization() {
        stopAnimation();
        ringsInput.value = String(DEFAULT_RINGS);
        activeNodes.clear();

        for (const nodeId of knownNodeIds) {
          activeNodes.add(nodeId);
        }

        saveState();
        render();
      }

      function resizeCanvas() {
        const ratio = window.devicePixelRatio || 1;
        const width = canvas.clientWidth;
        const height = canvas.clientHeight;
        canvas.width = Math.round(width * ratio);
        canvas.height = Math.round(height * ratio);
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
      }

      function render() {
        resizeCanvas();

        const totalRings = Number(ringsInput.value);
        ringCount.value = String(totalRings);

        const width = canvas.clientWidth;
        const height = canvas.clientHeight;
        context.clearRect(0, 0, width, height);

        scene = buildScene(totalRings, width, height, jetMode);
        syncActiveNodes(scene.nodes);

        for (const jet of scene.jets) {
          const active = activeNodes.has(jet.controllerId);
          const hovered = jet.controllerId === hoveredNodeId;
          drawJet(context, jet, active, hovered);
        }

        drawOutlineGlow(context, scene, activeNodes);
        drawStepperGlow(context, scene, stepperPositionPercent);

        updateStats();
        drawDistanceLabels(context, scene.nodes, buildDistanceMap(scene), labelMode);
        drawChannelLabels(context, scene, getMappedRelayNodeIds(scene), labelMode);
        drawAddressLabels(context, scene.nodes, labelMode);
        drawHoverNode(
          context,
          scene.nodes.find((node) => node.id === hoveredNodeId) || null,
        );
        syncRelayOutputs(scene);
      }

      canvas.addEventListener("mousemove", (event) => {
        const hitNode = findHitNode(event.clientX, event.clientY);
        hoveredNodeId = hitNode ? hitNode.id : null;
        canvas.style.cursor = hitNode ? "pointer" : "default";
        render();
      });

      canvas.addEventListener("mouseleave", () => {
        hoveredNodeId = null;
        canvas.style.cursor = "default";
        render();
      });

      canvas.addEventListener("click", (event) => {
        stopAnimation();
        const hitNode = findHitNode(event.clientX, event.clientY);
        if (!hitNode) {
          return;
        }

        if (activeNodes.has(hitNode.id)) {
          activeNodes.delete(hitNode.id);
        } else {
          activeNodes.add(hitNode.id);
        }

        saveState();
        render();
      });

      document.getElementById("sidebar-toggle").addEventListener("click", () => {
        document.getElementById("sidebar").classList.toggle("collapsed");
        render();
      });
      logPaneToggleButton.addEventListener("click", () => {
        setLogPaneCollapsed(!logPane.classList.contains("collapsed"));
        render();
      });
      clearDeviceLogsButton.addEventListener("click", clearDeviceLogs);

      resetButton.addEventListener("click", resetVisualization);
      midiConnectButton.addEventListener("click", async (event) => {
        if (event.target.closest(".connection-close")) {
          disconnectMidi();
          return;
        }
        await initMidi();
      });
      serialConnectButton.addEventListener("click", async (event) => {
        if (event.target.closest(".connection-close")) {
          await disconnectRelay();
          return;
        }
        await connectRelay();
      });
      stepperConnectButton.addEventListener("click", async (event) => {
        if (event.target.closest(".connection-close")) {
          await disconnectStepper();
          return;
        }
        await connectStepper();
      });
      relayCommandForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        try {
          await sendRelayCommand(relayCommandInput.value);
          relayCommandInput.value = "";
        } catch (error) {
          console.error(error);
          relayStatusMessage = error?.message || "Relay command failed";
          updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
        }
      });
      stepperCommandForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        try {
          await sendStepperCommand(stepperCommandInput.value);
          stepperCommandInput.value = "";
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper command failed";
          updateStepperUi();
        }
      });
      stepperHomeButton.addEventListener("click", homeStepper);
      jetModeSelect.addEventListener("change", () => {
        stopAnimation();
        jetMode = jetModeSelect.value;
        saveState();
        render();
      });
      allOnButton.addEventListener("click", () => {
        if (!scene) return;
        stopAnimation();
        for (const node of scene.nodes) {
          activeNodes.add(node.id);
        }
        saveState();
        render();
      });
      allOffButton.addEventListener("click", () => {
        stopAnimation();
        activeNodes.clear();
        saveState();
        render();
      });
      labelModeSelect.addEventListener("change", () => {
        labelMode = labelModeSelect.value;
        saveState();
        render();
      });
      function updateEditorFromSequence() {
        animationEditor.value = getScriptForSequence(selectedSequenceId);
        editorError.style.display = "none";
      }

      sequenceSelect.addEventListener("change", () => {
        selectedSequenceId = sequenceSelect.value;
        updateEditorFromSequence();
        saveState();
        if (animationTimerId !== null) {
          startAnimation();
        }
      });
      editorToggleButton.addEventListener("click", () => {
        const open = editorPanel.style.display === "none";
        editorPanel.style.display = open ? "" : "none";
        editorToggleButton.textContent = open
          ? "Hide Script"
          : "Edit Script";
      });
      document.getElementById("script-help-button").addEventListener("click", () => {
        const help = document.getElementById("script-help");
        help.style.display = help.style.display === "none" ? "" : "none";
      });
      animationEditor.addEventListener("keydown", (event) => {
        if (event.key === "Tab") {
          event.preventDefault();
          const start = animationEditor.selectionStart;
          const end = animationEditor.selectionEnd;
          animationEditor.value =
            animationEditor.value.substring(0, start) +
            "  " +
            animationEditor.value.substring(end);
          animationEditor.selectionStart = animationEditor.selectionEnd =
            start + 2;
          animationEditor.dispatchEvent(new Event("input"));
        }
      });
      animationEditor.addEventListener("input", () => {
        const script = animationEditor.value;
        const builtin = builtinScripts[selectedSequenceId];
        if (script === builtin) {
          delete customScripts[selectedSequenceId];
        } else {
          customScripts[selectedSequenceId] = script;
        }
        saveState();
        if (animationTimerId !== null) {
          startAnimation();
        }
      });
      restoreOriginalButton.addEventListener("click", () => {
        delete customScripts[selectedSequenceId];
        updateEditorFromSequence();
        saveState();
        if (animationTimerId !== null) {
          startAnimation();
        }
      });
      playPauseButton.addEventListener("click", () => {
        if (animationTimerId === null) {
          startAnimation();
        } else {
          stopAnimation();
        }
      });
      loopToggleButton.addEventListener("click", () => {
        animationLoopEnabled = !animationLoopEnabled;
        updateLoopToggleButton();
        saveState();
      });
      speedSlider.addEventListener("input", () => {
        animationSpeed = Number(speedSlider.value);
        updateSpeedReadout();
        saveState();
      });
      stepperPositionSlider.addEventListener("input", () => {
        setStepperBasePosition(Number(stepperPositionSlider.value));
      });
      stepperPositionSlider.addEventListener("change", async () => {
        setStepperBasePosition(Number(stepperPositionSlider.value), {
          send: false,
        });
        if (stepperPort !== null) {
          await flushStepperPositionSend();
        }
      });

      ringsInput.addEventListener("input", () => {
        stopAnimation();
        saveState();
        render();
      });
      if ("serial" in navigator) {
        navigator.serial.addEventListener("disconnect", async (event) => {
          if (event.target === relayPort) {
            await disconnectRelay();
          }
          if (event.target === stepperPort) {
            await disconnectStepper();
          }
        });
      }
      window.addEventListener("pagehide", () => {
        disconnectRelay().catch((error) => console.error(error));
        disconnectStepper().catch((error) => console.error(error));
      });
      window.addEventListener("beforeunload", () => {
        disconnectRelay().catch((error) => console.error(error));
        disconnectStepper().catch((error) => console.error(error));
      });
      window.addEventListener("resize", render);
      document.getElementById("sidebar").addEventListener("transitionend", render);

      const savedState = loadState();
      if (savedState?.rings !== null) {
        const min = Number(ringsInput.min);
        const max = Number(ringsInput.max);
        const clamped = Math.min(Math.max(savedState.rings, min), max);
        ringsInput.value = String(clamped);
      }

      if (savedState?.activeNodeIds) {
        activeNodes.clear();
        for (const nodeId of savedState.activeNodeIds) {
          activeNodes.add(nodeId);
        }
      }

      if (savedState?.knownNodeIds) {
        knownNodeIds.clear();
        for (const nodeId of savedState.knownNodeIds) {
          knownNodeIds.add(nodeId);
        }
      }

      if (typeof savedState?.jetMode === "string") {
        jetMode = savedState.jetMode;
      }

      if (typeof savedState?.labelMode === "string") {
        labelMode = savedState.labelMode;
      }

      if (typeof savedState?.animationLoopEnabled === "boolean") {
        animationLoopEnabled = savedState.animationLoopEnabled;
      }

      if (typeof savedState?.selectedSequenceId === "string") {
        selectedSequenceId = savedState.selectedSequenceId;
      }

      if (savedState?.customScripts) {
        Object.assign(customScripts, savedState.customScripts);
      }

      if (Number.isFinite(savedState?.animationSpeed)) {
        animationSpeed = Math.min(Math.max(savedState.animationSpeed, 1), 18);
      }

      if (Number.isFinite(savedState?.stepperPositionPercent)) {
        stepperBasePositionPercent = Math.min(
          Math.max(savedState.stepperPositionPercent, 0),
          100,
        );
        stepperPositionPercent = stepperBasePositionPercent;
      }

      if (Number.isFinite(savedState?.stepperTravelSteps)) {
        stepperTravelSteps = Math.max(savedState.stepperTravelSteps, 0);
      }

      if (Number.isFinite(savedState?.stepperEnvelopeAttackMs)) {
        stepperEnvelope.attackMs = Math.min(
          Math.max(savedState.stepperEnvelopeAttackMs, 20),
          2000,
        );
      }

      if (Number.isFinite(savedState?.stepperEnvelopeDecayMs)) {
        stepperEnvelope.decayMs = Math.min(
          Math.max(savedState.stepperEnvelopeDecayMs, 20),
          2000,
        );
      }

      if (Number.isFinite(savedState?.stepperEnvelopeSustainLevel)) {
        stepperEnvelope.sustainLevel = Math.min(
          Math.max(savedState.stepperEnvelopeSustainLevel, 0),
          1,
        );
      }

      if (Number.isFinite(savedState?.stepperEnvelopeReleaseMs)) {
        stepperEnvelope.releaseMs = Math.min(
          Math.max(savedState.stepperEnvelopeReleaseMs, 20),
          2000,
        );
      }

      stepperEnvelope.originPercent = stepperBasePositionPercent;

      jetModeSelect.value = jetMode;
      sequenceSelect.value = selectedSequenceId;
      updateEditorFromSequence();
      speedSlider.value = String(animationSpeed);
      stepperPositionSlider.value = stepperPositionPercent.toFixed(1);
      updateLabelModeSelect();
      updateSpeedReadout();
      updateStepperReadout();
      updateStepperTravelReadout();
      updateStepperEnvelopeUi();
      updatePlayPauseButton();
      updateLoopToggleButton();
      updateMidiUi();
      updateSerialUi(0);
      updateStepperUi();
      let logPaneCollapsed = true;
      try {
        logPaneCollapsed =
          window.localStorage.getItem(LOG_PANE_COLLAPSED_KEY) !== "0";
      } catch {
        logPaneCollapsed = true;
      }
      setLogPaneCollapsed(logPaneCollapsed);
      for (const role of Object.keys(deviceLogs)) {
        renderDeviceLog(role);
      }
      render();
      autoConnectRelay();
      autoConnectStepper();
      initMidi();
