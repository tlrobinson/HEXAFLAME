// @ts-nocheck
import "./styles.css";
import { flushSync } from "react-dom";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import {
  createConnection,
  type Connection,
  type MappingTarget,
} from "./features/connections/connection-model";
import {
  setConnectionCallbacks,
  setConnectionsSnapshot,
} from "./features/connections/connections-store";
import { formatSerialBytes } from "./features/logs/log-format";
import {
  appendDeviceLog,
  setDeviceCommandHandler,
  setDeviceCommandEnabled,
} from "./features/logs/log-store";
import { builtinScripts } from "./features/animation/builtin-scripts";
import {
  buildDistanceMap,
  buildScene,
  getCenterNode,
} from "./scene/hex-grid";
import {
  createAdsrEnvelope,
  getAdsrOutputValue,
  releaseAdsrEnvelope,
  tickAdsrEnvelope,
  triggerAdsrEnvelope,
} from "./control/adsr";
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
  getSerialPortKey,
  getRelayMappedNodeIds,
  parseRelayCommandInput,
  relayStatesEqual,
  rememberSerialPortRole,
} from "./devices/relay";
import {
  STEPPER_ENVELOPE_SEND_DELAY_MS,
  STEPPER_SEND_DELAY_MS,
  STEPPER_SERIAL_BAUD,
  buildStepperAdsrCommand,
  buildStepperHomeCommand,
  buildStepperReleaseCommand,
  buildStepperTimedPositionCommand,
  buildStepperJsonRpcRequest,
  parseStepperCommandInput,
  parseStepperProtocolLine,
  readStepperTextLines,
  writeStepperTextCommand,
} from "./devices/stepper";
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
import { setStats } from "./features/sidebar/stats-store";
import {
  setGridCallbacks,
  setGridSnapshot,
} from "./features/sidebar/grid-store";
import {
  sequenceOptions,
  setAnimationControlsCallbacks,
  setAnimationControlsSnapshot,
} from "./features/animation/animation-controls-store";
import {
  setScriptEditorCallbacks,
  setScriptEditorSnapshot,
} from "./features/sidebar/script-editor-store";
import {
  setMidiCallbacks,
  setMidiSnapshot,
  setMidiStatus,
} from "./features/midi/midi-store";
import {
  setSidebarCallbacks,
  setSidebarCollapsed,
} from "./features/sidebar/sidebar-store";
import { setEnvelopeSnapshot } from "./features/envelope/envelope-store";
import {
  getCanvasElement,
  setCanvasCallbacks,
} from "./features/canvas/canvas-store";
import { setLifecycleCallbacks } from "./features/lifecycle/lifecycle-store";

      setCanvasCallbacks({
        onClick: (event) => {
          stopAnimation();
          const hitNode = findHitNode(event.clientX, event.clientY);
          if (!hitNode) {
            return;
          }

          if (mappingTarget) {
            const connection = connections.find(
              (candidate) => candidate.id === mappingTarget.connectionId,
            );
            const channel = connection?.channels[mappingTarget.channelIndex];
            if (channel) {
              channel.jetId = hitNode.id;
              mappingTarget = null;
              saveState();
              renderConnections();
              render();
            }
            return;
          }

          if (activeNodes.has(hitNode.id)) {
            activeNodes.delete(hitNode.id);
          } else {
            activeNodes.add(hitNode.id);
          }

          saveState();
          render();
        },
        onMouseLeave: () => {
          hoveredNodeId = null;
          canvas.style.cursor = "default";
          render();
        },
        onMouseMove: (event) => {
          const hitNode = findHitNode(event.clientX, event.clientY);
          hoveredNodeId = hitNode ? hitNode.id : null;
          canvas.style.cursor = hitNode ? "pointer" : "default";
          render();
        },
      });

      function disconnectAllDevices() {
        for (const connection of getRelayConnections()) {
          disconnectRelay(connection).catch((error) => console.error(error));
        }
        disconnectStepper().catch((error) => console.error(error));
      }

      setLifecycleCallbacks({
        onBeforeUnload: disconnectAllDevices,
        onPageHide: disconnectAllDevices,
        onResize: () => {
          render();
        },
        onSerialDisconnect: (event) => {
          void (async () => {
            for (const connection of getRelayConnections()) {
              if (event.target === connection.port) {
                await disconnectRelay(connection);
              }
            }
            if (event.target === stepperPort) {
              await disconnectStepper();
            }
          })();
        },
      });

      flushSync(() => {
        createRoot(document.getElementById("app-root")).render(<App />);
      });

      const canvas = getCanvasElement();
      const context = canvas.getContext("2d");
      const midiStatus = {
        set textContent(value) {
          setMidiStatus(value);
        },
      };
      const STORAGE_KEY = "hexagon-rings-state";
      const RELAY_PORT_KEY = `${STORAGE_KEY}:relay-port`;
      const STEPPER_PORT_KEY = `${STORAGE_KEY}:stepper-port`;
      const HIT_RADIUS = 10;
      const DEFAULT_RINGS = 2;
      const MIN_RINGS = 1;
      const MAX_RINGS = 12;
      const activeNodes = new Set();
      const knownNodeIds = new Set();
      let scene = null;
      let hoveredNodeId = null;
      let rings = 4;
      let jetMode = "all";
      let labelMode = "address";
      const customScripts = {};
      let scriptEditorOpen = false;
      let scriptHelpOpen = false;
      let selectedSequenceId = "ripple";
      let animationSpeed = 12;
      let animationLoopEnabled = true;
      let animationFrames = [];
      let animationFrameIndex = 0;
      let animationTimerId = null;
      let midiAccess = null;
      const midiInputs = new Map();
      const midiHeldNotes = new Set();
      const connections: Connection[] = [];
      let mappingTarget: MappingTarget = null;
      let activeStepperConnectionId = null;
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
      const stepperEnvelope = createAdsrEnvelope({
        attackMs: 180,
        decayMs: 220,
        sustainLevel: 0.55,
        releaseMs: 320,
      });
      stepperEnvelope.originValue = 50;
      stepperEnvelope.targetValue = 100;

      function getRelayConnections() {
        return connections.filter((connection) => connection.type === "relay");
      }

      function getStepperConnections() {
        return connections.filter((connection) => connection.type === "stepper");
      }

      function getPrimaryRelayConnection() {
        return getRelayConnections()[0] || null;
      }

      function getPrimaryStepperConnection() {
        return (
          connections.find((connection) => connection.id === activeStepperConnectionId) ||
          getStepperConnections().find((connection) => connection.port !== null) ||
          getStepperConnections()[0] ||
          null
        );
      }

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
        setDeviceCommandEnabled(
          "relay",
          serialSupported && Boolean(getPrimaryRelayConnection()?.port),
        );
        updateConnectionDom();
      }

      function updateStepperReadout() {
        const stepper = getPrimaryStepperConnection();
        if (stepper) {
          stepper.channels[0].positionPercent = stepperPositionPercent;
        }
        updateConnectionDom();
      }

      function updateStepperTravelReadout() {
        const stepper = getPrimaryStepperConnection();
        if (stepper) {
          stepper.channels[0].travelSteps = stepperTravelSteps;
        }
        updateConnectionDom();
      }

      function canControlStepperPosition() {
        return stepperPort !== null && stepperHomed;
      }

      function cancelStepperPositionQueue() {
        if (stepperPositionSendTimerId !== null) {
          window.clearTimeout(stepperPositionSendTimerId);
          stepperPositionSendTimerId = null;
        }
        stepperQueuedPosition = null;
      }

      function updateStepperHomedReadout() {
        const stepper = getPrimaryStepperConnection();
        if (stepper) {
          stepper.channels[0].homed = stepperHomed;
          stepper.channels[0].state = stepperPort === null ? "Unknown" : stepperHomed ? "Homed" : "Not homed";
        }
        updateConnectionDom();
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

        const path =
          `M ${graphLeft} ${graphBottom} ` +
          `L ${attackX.toFixed(1)} ${graphTop} ` +
          `L ${decayX.toFixed(1)} ${sustainY.toFixed(1)} ` +
          `L ${sustainEndX.toFixed(1)} ${sustainY.toFixed(1)} ` +
          `L ${graphRight} ${graphBottom}`;

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

        const envelopeLevel = stepperEnvelope.active
          ? stepperEnvelope.currentLevel
          : 0;
        const actualLevel = envelopeLevel * stepperEnvelope.velocityScale;
        const markerY = graphBottom - graphHeight * actualLevel;
        setEnvelopeSnapshot({
          attackLabel: `A ${formatEnvelopeTime(stepperEnvelope.attackMs)}`,
          decayLabel: `D ${formatEnvelopeTime(stepperEnvelope.decayMs)}`,
          fillPath: fillSegments.join(" "),
          markerX: point.x.toFixed(1),
          markerY: markerY.toFixed(1),
          path,
          releaseLabel: `R ${formatEnvelopeTime(stepperEnvelope.releaseMs)}`,
          sustainLabel: `S ${sustainPercent}%`,
          sweepX: point.x.toFixed(1),
          sweepY1: point.y.toFixed(1),
          sweepY2: graphBottom.toFixed(1),
        });
      }

      function applyStepperOutputPosition(
        percent,
        { send = true, save = true, sendDelayMs = STEPPER_SEND_DELAY_MS } = {},
      ) {
        stepperPositionPercent = Math.min(Math.max(percent, 0), 100);
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

      function applyCenterEnvelopeOutput(
        send = true,
        sendDelayMs = STEPPER_ENVELOPE_SEND_DELAY_MS,
      ) {
        // The ADSR module is controller-agnostic; this adapter maps it to
        // the current center-spoke actuator, which is still Stepper 1 today.
        applyStepperOutputPosition(getAdsrOutputValue(stepperEnvelope), {
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

      function tickCenterEnvelope(now) {
        const result = tickAdsrEnvelope(stepperEnvelope, now);
        if (!result.active && !result.stopped) {
          stepperEnvelopeFrameId = null;
          return;
        }

        if (result.stopped) {
          stepperEnvelopeFrameId = null;
          applyStepperOutputPosition(stepperEnvelope.originValue, {
            send: false,
            save: false,
            sendDelayMs: STEPPER_ENVELOPE_SEND_DELAY_MS,
          });
          updateStepperEnvelopeUi(now);
          saveState();
          return;
        }

        applyCenterEnvelopeOutput(false, STEPPER_ENVELOPE_SEND_DELAY_MS);
        updateStepperEnvelopeUi(now);
        stepperEnvelopeFrameId = window.requestAnimationFrame(tickCenterEnvelope);
      }

      function getCenterEnvelopeOutputForLevel(level) {
        return (
          stepperEnvelope.originValue +
          (stepperEnvelope.targetValue - stepperEnvelope.originValue) *
            stepperEnvelope.velocityScale *
            level
        );
      }

      async function sendCenterAdsrCommand() {
        if (!canControlStepperPosition()) {
          return false;
        }

        cancelStepperPositionQueue();
        const command = buildStepperAdsrCommand({
          attackPercent: getCenterEnvelopeOutputForLevel(1),
          attackMs: stepperEnvelope.attackMs,
          decayPercent: getCenterEnvelopeOutputForLevel(
            stepperEnvelope.sustainLevel,
          ),
          decayMs: stepperEnvelope.decayMs,
          sustainMs: 0,
          releasePercent: stepperEnvelope.originValue,
          releaseMs: stepperEnvelope.releaseMs,
        });

        try {
          await writeStepperCommand(command);
          stepperStatusMessage = "Stepper ADSR started";
          updateStepperUi();
          return true;
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper ADSR failed";
          updateStepperUi();
          return false;
        }
      }

      async function sendCenterReleaseCommand() {
        if (!canControlStepperPosition()) {
          return false;
        }

        cancelStepperPositionQueue();
        const command = buildStepperReleaseCommand(
          stepperEnvelope.originValue,
          stepperEnvelope.releaseMs,
        );

        try {
          await writeStepperCommand(command);
          stepperStatusMessage = "Stepper release started";
          updateStepperUi();
          return true;
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper release failed";
          updateStepperUi();
          return false;
        }
      }

      function startCenterEnvelope(velocity = 127) {
        const now = performance.now();
        triggerAdsrEnvelope(stepperEnvelope, {
          velocity,
          originValue: stepperBasePositionPercent,
          targetValue: 100,
          now,
        });
        updateStepperEnvelopeUi(now);
        applyCenterEnvelopeOutput(false, STEPPER_ENVELOPE_SEND_DELAY_MS);
        void sendCenterAdsrCommand();
        if (stepperEnvelopeFrameId === null) {
          stepperEnvelopeFrameId = window.requestAnimationFrame(
            tickCenterEnvelope,
          );
        }
        midiStatus.textContent =
          `Center ADSR ${Math.round(stepperEnvelope.velocityScale * 100)}%`;
        return canControlStepperPosition();
      }

      function releaseCenterEnvelope() {
        if (!releaseAdsrEnvelope(stepperEnvelope, performance.now())) {
          return;
        }

        updateStepperEnvelopeUi();
        void sendCenterReleaseCommand();
        midiStatus.textContent = "Center Release";
      }

      function updateStepperUi() {
        const serialSupported = "serial" in navigator;
        setDeviceCommandEnabled("stepper", serialSupported && stepperPort !== null);
        updateStepperHomedReadout();
        updateConnectionDom();
      }

      function updateMidiUi() {
        const midiSupported = Boolean(navigator.requestMIDIAccess);
        setMidiSnapshot({
          connected: midiInputs.size > 0,
          supported: midiSupported,
        });
        if (!midiSupported) {
          midiStatus.textContent = "Web MIDI not supported";
        }
      }

      function renderConnections() {
        setConnectionsSnapshot({
          activeNodeIds: new Set(activeNodes),
          connections: [...connections],
          mappingTarget,
          serialSupported: "serial" in navigator,
        });
      }

      function updateConnectionDom() {
        renderConnections();
      }

      setConnectionCallbacks({
        onAddRelay: () => {
          connections.push(createConnection("relay"));
          saveState();
          renderConnections();
        },
        onAddStepper: () => {
          connections.push(createConnection("stepper"));
          saveState();
          renderConnections();
        },
        onConnect: (connection) => {
          if (connection.type === "relay") {
            void connectRelay(connection);
          } else {
            void connectStepper(connection);
          }
        },
        onDisconnect: (connection) => {
          if (connection.type === "relay") {
            void disconnectRelay(connection);
          } else {
            void disconnectStepper();
          }
        },
        onHome: (connection) => {
          activeStepperConnectionId = connection.id;
          void homeStepper();
        },
        onMap: (connection, channel) => {
          mappingTarget = {
            connectionId: connection.id,
            channelIndex: channel.index,
          };
          renderConnections();
        },
        onPositionCommit: (connection, channel, positionPercent) => {
          channel.positionPercent = positionPercent;
          if (connection === getPrimaryStepperConnection()) {
            setStepperBasePosition(positionPercent, { send: false });
            void flushStepperPositionSend();
          }
          renderConnections();
        },
        onPositionInput: (connection, channel, positionPercent) => {
          channel.positionPercent = positionPercent;
          if (connection === getPrimaryStepperConnection()) {
            setStepperBasePosition(positionPercent);
          }
          renderConnections();
        },
        onToggleConfig: (connection) => {
          connection.expanded = !connection.expanded;
          saveState();
          renderConnections();
        },
      });

      setGridCallbacks({
        onAllOff: () => {
          stopAnimation();
          activeNodes.clear();
          saveState();
          render();
        },
        onAllOn: () => {
          if (!scene) return;
          stopAnimation();
          for (const node of scene.nodes) {
            activeNodes.add(node.id);
          }
          saveState();
          render();
        },
        onJetModeChange: (nextJetMode) => {
          stopAnimation();
          jetMode = nextJetMode;
          updateGridControls();
          saveState();
          render();
        },
        onLabelModeChange: (nextLabelMode) => {
          labelMode = nextLabelMode;
          updateGridControls();
          saveState();
          render();
        },
        onReset: resetVisualization,
        onRingsChange: (nextRings) => {
          stopAnimation();
          rings = Math.min(Math.max(nextRings, MIN_RINGS), MAX_RINGS);
          updateGridControls();
          saveState();
          render();
        },
      });

      setAnimationControlsCallbacks({
        onLoopToggle: () => {
          animationLoopEnabled = !animationLoopEnabled;
          updateLoopToggleButton();
          saveState();
        },
        onPlayPause: () => {
          if (animationTimerId === null) {
            startAnimation();
          } else {
            stopAnimation();
          }
        },
        onSequenceChange: (sequenceId) => {
          selectedSequenceId = sequenceId;
          updateEditorFromSequence();
          updateAnimationControls();
          saveState();
          if (animationTimerId !== null) {
            startAnimation();
          }
        },
        onSpeedChange: (speed) => {
          animationSpeed = speed;
          updateSpeedReadout();
          saveState();
        },
      });

      setScriptEditorCallbacks({
        onChange: (script) => {
          const builtin = builtinScripts[selectedSequenceId];
          if (script === builtin) {
            delete customScripts[selectedSequenceId];
          } else {
            customScripts[selectedSequenceId] = script;
          }
          setScriptEditorSnapshot({ script });
          saveState();
          if (animationTimerId !== null) {
            startAnimation();
          }
        },
        onRestore: () => {
          delete customScripts[selectedSequenceId];
          updateEditorFromSequence();
          saveState();
          if (animationTimerId !== null) {
            startAnimation();
          }
        },
        onToggleHelp: () => {
          scriptHelpOpen = !scriptHelpOpen;
          setScriptEditorSnapshot({ helpOpen: scriptHelpOpen });
        },
        onToggleOpen: () => {
          scriptEditorOpen = !scriptEditorOpen;
          setScriptEditorSnapshot({ open: scriptEditorOpen });
        },
      });

      setMidiCallbacks({
        onConnect: () => {
          void initMidi();
        },
        onDisconnect: () => {
          disconnectMidi();
        },
      });

      let sidebarCollapsed = false;
      setSidebarCallbacks({
        onToggle: () => {
          sidebarCollapsed = !sidebarCollapsed;
          setSidebarCollapsed(sidebarCollapsed);
          render();
        },
        onTransitionEnd: () => {
          render();
        },
      });
      setSidebarCollapsed(sidebarCollapsed);

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
        logSerialRx("stepper", line);
        const update = parseStepperProtocolLine(line);
        const connection = getPrimaryStepperConnection();
        if (update.homed !== undefined) {
          stepperHomed = update.homed;
          if (connection) {
            connection.channels[0].homed = update.homed;
            connection.channels[0].state = update.homed ? "Homed" : "Not homed";
          }
        }

        if (update.travelSteps !== undefined) {
          stepperTravelSteps = update.travelSteps;
          if (connection) {
            connection.channels[0].travelSteps = update.travelSteps;
          }
          updateStepperTravelReadout();
          saveState();
        }

        if (update.positionPercent !== undefined) {
          if (connection) {
            connection.channels[0].positionPercent = update.positionPercent;
          }
          syncStepperPositionFromBoard(update.positionPercent);
        }

        if (update.statusMessage) {
          stepperStatusMessage = update.statusMessage;
          if (connection) {
            connection.status = update.statusMessage;
          }
        }
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
          const centerEnvelopeAttackMs = Number(
            parsed.centerEnvelopeAttackMs ?? parsed.stepperEnvelopeAttackMs,
          );
          const centerEnvelopeDecayMs = Number(
            parsed.centerEnvelopeDecayMs ?? parsed.stepperEnvelopeDecayMs,
          );
          const centerEnvelopeSustainLevel = Number(
            parsed.centerEnvelopeSustainLevel ??
              parsed.stepperEnvelopeSustainLevel,
          );
          const centerEnvelopeReleaseMs = Number(
            parsed.centerEnvelopeReleaseMs ?? parsed.stepperEnvelopeReleaseMs,
          );
          const savedCustomScripts =
            parsed.customScripts &&
            typeof parsed.customScripts === "object"
              ? parsed.customScripts
              : {};
          const savedConnections = Array.isArray(parsed.connections)
            ? parsed.connections.filter(
                (connection) =>
                  connection &&
                  typeof connection === "object" &&
                  (connection.type === "relay" || connection.type === "stepper"),
              )
            : null;

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
            centerEnvelopeAttackMs: Number.isFinite(centerEnvelopeAttackMs)
              ? centerEnvelopeAttackMs
              : 180,
            centerEnvelopeDecayMs: Number.isFinite(centerEnvelopeDecayMs)
              ? centerEnvelopeDecayMs
              : 220,
            centerEnvelopeSustainLevel: Number.isFinite(centerEnvelopeSustainLevel)
              ? centerEnvelopeSustainLevel
              : 0.55,
            centerEnvelopeReleaseMs: Number.isFinite(centerEnvelopeReleaseMs)
              ? centerEnvelopeReleaseMs
              : 320,
            customScripts: savedCustomScripts,
            connections: savedConnections,
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
              rings,
              activeNodeIds: [...activeNodes],
              knownNodeIds: [...knownNodeIds],
              jetMode,
              labelMode,
              animationLoopEnabled,
              selectedSequenceId,
              animationSpeed,
              stepperPositionPercent: stepperBasePositionPercent,
              stepperTravelSteps,
              centerEnvelopeAttackMs: stepperEnvelope.attackMs,
              centerEnvelopeDecayMs: stepperEnvelope.decayMs,
              centerEnvelopeSustainLevel: stepperEnvelope.sustainLevel,
              centerEnvelopeReleaseMs: stepperEnvelope.releaseMs,
              connections: connections.map((connection) => ({
                id: connection.id,
                type: connection.type,
                name: connection.name,
                expanded: connection.expanded,
                portKey: connection.portKey,
                channels: connection.channels.map((channel) => ({
                  index: channel.index,
                  jetId: channel.jetId,
                  homed: channel.homed,
                  travelSteps: channel.travelSteps,
                  positionPercent: channel.positionPercent,
                })),
              })),
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

      function updateGridControls() {
        setGridSnapshot({
          jetMode,
          labelMode,
          rings,
        });
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
          setStats({
            outline: { total: 0, visible: 0 },
            spoke: { total: 0, visible: 0 },
          });
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

        setStats(counts);
      }

      function updateSpeedReadout() {
        updateAnimationControls();
      }

      function updatePlayPauseButton() {
        updateAnimationControls();
      }

      function updateLoopToggleButton() {
        updateAnimationControls();
      }

      function updateAnimationControls() {
        setAnimationControlsSnapshot({
          loopEnabled: animationLoopEnabled,
          playing: animationTimerId !== null,
          selectedSequenceId,
          speed: animationSpeed,
        });
      }

      function getMappedRelayNodeIds(currentScene) {
        const mapped = [];
        for (const connection of getRelayConnections()) {
          for (const channel of connection.channels) {
            if (channel.jetId) {
              mapped.push(channel.jetId);
            }
          }
        }
        if (mapped.length > 0) {
          return mapped;
        }

        return getRelayMappedNodeIds(
          currentScene,
          buildDistanceMap(currentScene),
          angleFromCenter,
        );
      }

      function getRelayStateSnapshot(connection) {
        const states = Array(RELAY_CHANNEL_COUNT).fill(false);
        for (const channel of connection.channels) {
          states[channel.index] = channel.jetId ? activeNodes.has(channel.jetId) : false;
        }
        return {
          mappedNodeIds: connection.channels
            .filter((channel) => channel.jetId)
            .map((channel) => channel.jetId),
          states,
        };
      }

      async function writeRelayFrame(frame, connection = getPrimaryRelayConnection()) {
        if (!connection?.port?.writable) {
          throw new Error("Relay port is not connected");
        }

        logSerialTx("relay", frame);
        const writer = connection.port.writable.getWriter();
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

        const connection = getPrimaryRelayConnection();
        await writeRelayFrame(frame, connection);
        if (connection) {
          connection.status = "Relay command sent";
          relayStatusMessage = connection.status;
        }
        updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
      }

      async function processRelaySyncQueue(connection = getPrimaryRelayConnection()) {
        if (!connection || connection.syncInProgress || connection.port === null) {
          return;
        }

        connection.syncInProgress = true;

        try {
          while (connection.stateQueue.length > 0 && connection.port !== null) {
            const nextStates = connection.stateQueue.shift();
            connection.lastStates = [...nextStates];
            await writeRelayFrame(buildRelayWriteMultipleFrame(nextStates), connection);
            await new Promise((resolve) => setTimeout(resolve, 10));
          }

          if (connection.port !== null) {
            connection.status = "Relay sync connected";
            relayStatusMessage = connection.status;
          }
        } catch (error) {
          console.error(error);
          connection.lastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
          connection.status = "Relay sync error";
          relayStatusMessage = connection.status;
        } finally {
          connection.syncInProgress = false;
          updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
        }
      }

      function syncRelayOutputs(currentScene) {
        updateSerialUi(getMappedRelayNodeIds(currentScene).length);

        for (const connection of getRelayConnections()) {
          if (connection.port === null) {
            continue;
          }

          const { states } = getRelayStateSnapshot(connection);
          const queuedStates =
            connection.stateQueue[connection.stateQueue.length - 1] ||
            connection.lastStates;
          if (relayStatesEqual(queuedStates, states)) {
            continue;
          }

          connection.stateQueue.push([...states]);
          processRelaySyncQueue(connection);
        }
      }

      async function readRelayLoop(connection, port) {
        while (connection.port === port && port.readable) {
          const reader = port.readable.getReader();
          connection.reader = reader;
          try {
            while (connection.port === port) {
              const { value, done } = await reader.read();
              if (done) {
                break;
              }
              if (value?.length > 0) {
                logSerialRx("relay", value);
              }
            }
          } catch (error) {
            if (connection.port === port) {
              console.error(error);
              connection.status = "Relay read error";
              relayStatusMessage = connection.status;
              updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
            }
          } finally {
            connection.reader = null;
            reader.releaseLock();
          }
        }
      }

      async function openRelayPort(port, connection = getPrimaryRelayConnection()) {
        try {
          if (!connection) {
            return;
          }
          if (port === stepperPort) {
            connection.status = "Selected port is already in use by Stepper";
            relayStatusMessage = connection.status;
            updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
            return;
          }

          connection.port = port;
          relayPort = getPrimaryRelayConnection()?.port || null;
          await connection.port.open({
            baudRate: 115200,
            dataBits: 8,
            stopBits: 1,
            parity: "none",
            flowControl: "none",
          });
          connection.portKey = getSerialPortKey(port);
          rememberSerialPortRole("relay", port, RELAY_PORT_KEY, STEPPER_PORT_KEY);
          connection.lastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
          connection.status = "Relay sync connected";
          relayStatusMessage = connection.status;
          appendDeviceLog("relay", "event", "connected");
          readRelayLoop(connection, connection.port);
          if (scene) {
            syncRelayOutputs(scene);
          } else {
            updateSerialUi(0);
          }
          saveState();
        } catch (error) {
          connection.port = null;
          relayPort = getPrimaryRelayConnection()?.port || null;
          connection.status = "Relay connection failed";
          relayStatusMessage = connection.status;
          updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
          console.error(error);
        }
      }

      async function connectRelay(connection = getPrimaryRelayConnection()) {
        if (!("serial" in navigator) || !connection || connection.port !== null) {
          return;
        }

        try {
          const ports = await navigator.serial.getPorts();
          const port =
            (connection.portKey
              ? ports.find((candidate) => getSerialPortKey(candidate) === connection.portKey)
              : null) ||
            findPreferredSerialPort(ports, "relay", RELAY_PORT_KEY, STEPPER_PORT_KEY) ||
            (await navigator.serial.requestPort());
          await openRelayPort(port, connection);
        } catch (error) {
          console.error(error);
        }
      }

      async function autoConnectRelay() {
        if (!("serial" in navigator)) {
          return;
        }

        try {
          const ports = await navigator.serial.getPorts();
          for (const connection of getRelayConnections()) {
            if (connection.port !== null || !connection.portKey) {
              continue;
            }
            const port = ports.find((candidate) => getSerialPortKey(candidate) === connection.portKey);
            if (port) {
              await openRelayPort(port, connection);
            }
          }
        } catch (error) {
          console.error(error);
        }
      }

      async function disconnectRelay(connection = getPrimaryRelayConnection()) {
        if (!connection) {
          return;
        }
        const wasConnected = connection.port !== null;
        connection.stateQueue = [];
        connection.syncInProgress = false;
        if (connection.reader !== null) {
          try {
            await connection.reader.cancel();
          } catch (error) {
            console.error(error);
          }
        }

        if (connection.port !== null) {
          try {
            await connection.port.close();
          } catch (error) {
            console.error(error);
          }
        }

        connection.port = null;
        connection.reader = null;
        relayPort = getPrimaryRelayConnection()?.port || null;
        connection.lastStates = Array(RELAY_CHANNEL_COUNT).fill(null);
        connection.status = "Relay sync disconnected";
        relayStatusMessage = connection.status;
        if (wasConnected) {
          appendDeviceLog("relay", "event", "disconnected");
        }
        updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
      }

      async function writeStepperCommand(command) {
        await writeStepperTextCommand(stepperPort, command, (payload) => {
          logSerialTx("stepper", payload);
        });
      }

      async function sendStepperCommand(command) {
        const trimmed = command.trim();
        if (!trimmed) {
          return;
        }

        const request = parseStepperCommandInput(trimmed);
        if (request === null) {
          return;
        }

        await writeStepperCommand(request);
        stepperStatusMessage = `Stepper command sent: ${trimmed}`;
        updateStepperUi();
      }

      async function requestStepperStatus() {
        if (stepperPort === null) {
          return;
        }

        await writeStepperCommand(buildStepperJsonRpcRequest("status"));
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
            await writeStepperCommand(
              buildStepperTimedPositionCommand(nextPosition, STEPPER_SEND_DELAY_MS),
            );
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
          await writeStepperCommand(buildStepperHomeCommand());
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper home failed";
          updateStepperUi();
        }
      }

      async function readStepperLoop(port, connection = getPrimaryStepperConnection()) {
        await readStepperTextLines(port, {
          isActive: (currentPort) => stepperPort === currentPort,
          onReader: (reader) => {
            stepperReader = reader;
            if (connection) {
              connection.reader = reader;
            }
          },
          onLine: handleStepperLine,
          onError: (error) => {
            if (stepperPort === port) {
              console.error(error);
              stepperStatusMessage = "Stepper read error";
              updateStepperUi();
            }
          },
        });
      }

      async function openStepperPort(port, connection = getPrimaryStepperConnection()) {
        try {
          if (!connection) {
            return;
          }
          if (port === relayPort) {
            stepperStatusMessage = "Selected port is already in use by Relay";
            connection.status = stepperStatusMessage;
            updateStepperUi();
            return;
          }

          stepperPort = port;
          activeStepperConnectionId = connection.id;
          connection.port = port;
          await stepperPort.open({
            baudRate: STEPPER_SERIAL_BAUD,
            dataBits: 8,
            stopBits: 1,
            parity: "none",
            flowControl: "none",
          });
          connection.portKey = getSerialPortKey(port);
          rememberSerialPortRole("stepper", port, RELAY_PORT_KEY, STEPPER_PORT_KEY);
          stepperHomed = false;
          stepperTravelSteps = null;
          updateStepperTravelReadout();
          stepperStatusMessage = "Checking stepper status...";
          connection.status = stepperStatusMessage;
          connection.channels[0].state = "Checking";
          appendDeviceLog("stepper", "event", "connected");
          updateStepperUi();
          readStepperLoop(stepperPort, connection);
          await requestStepperStatus();
          saveState();
        } catch (error) {
          stepperPort = null;
          if (connection) {
            connection.port = null;
          }
          stepperHomed = false;
          stepperStatusMessage =
            error?.name === "InvalidStateError"
              ? "Selected port is already open"
              : "Stepper connection failed";
          if (connection) {
            connection.status = stepperStatusMessage;
          }
          updateStepperUi();
          console.error(error);
        }
      }

      async function connectStepper(connection = getPrimaryStepperConnection()) {
        if (
          !("serial" in navigator) ||
          !connection ||
          stepperPort !== null ||
          connection.connectInProgress
        ) {
          return;
        }

        activeStepperConnectionId = connection.id;
        connection.connectInProgress = true;
        stepperConnectInProgress = true;
        stepperStatusMessage = "Connecting stepper...";
        connection.status = stepperStatusMessage;
        updateStepperUi();

        try {
          const ports = await navigator.serial.getPorts();
          const port =
            (connection.portKey
              ? ports.find((candidate) => getSerialPortKey(candidate) === connection.portKey)
              : null) ||
            findPreferredSerialPort(ports, "stepper", RELAY_PORT_KEY, STEPPER_PORT_KEY) ||
            (await navigator.serial.requestPort());
          await openStepperPort(port, connection);
        } catch (error) {
          stepperStatusMessage = "Stepper disconnected";
          connection.status = stepperStatusMessage;
          updateStepperUi();
          console.error(error);
        } finally {
          connection.connectInProgress = false;
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

        const connection =
          getStepperConnections().find((candidate) => candidate.portKey) ||
          getPrimaryStepperConnection();
        if (!connection) {
          return;
        }
        activeStepperConnectionId = connection.id;
        connection.connectInProgress = true;
        stepperConnectInProgress = true;
        stepperStatusMessage = "Connecting stepper...";
        connection.status = stepperStatusMessage;
        updateStepperUi();

        try {
          const ports = await navigator.serial.getPorts();
          const port =
            (connection.portKey
              ? ports.find((candidate) => getSerialPortKey(candidate) === connection.portKey)
              : null) ||
            findPreferredSerialPort(ports, "stepper", RELAY_PORT_KEY, STEPPER_PORT_KEY);
          if (port) {
            await openStepperPort(port);
          } else {
            stepperStatusMessage = "Stepper disconnected";
            connection.status = stepperStatusMessage;
            updateStepperUi();
          }
        } catch (error) {
          stepperStatusMessage = "Stepper disconnected";
          connection.status = stepperStatusMessage;
          updateStepperUi();
          console.error(error);
        } finally {
          connection.connectInProgress = false;
          stepperConnectInProgress = false;
          updateStepperUi();
        }
      }

      async function disconnectStepper() {
        const connection = getPrimaryStepperConnection();
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
        if (connection) {
          connection.port = null;
          connection.reader = null;
        }
        activeStepperConnectionId = null;
        stepperHomed = false;
        stepperTravelSteps = null;
        updateStepperTravelReadout();
        stepperStatusMessage = "Stepper disconnected";
        if (connection) {
          connection.status = stepperStatusMessage;
          connection.channels[0].state = "Unknown";
          connection.channels[0].homed = false;
        }
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
      //   Note 36 triggers the center ADSR envelope, other mapped pads momentarily
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
            const sendsToStepper = startCenterEnvelope(data2);
            midiStatus.textContent = sendsToStepper
              ? `Center ADSR on (note ${data1}, vel ${data2})`
              : `Center ADSR visual only (note ${data1}, vel ${data2})`;
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
            releaseCenterEnvelope();
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
              `Center Attack: ${Math.round(stepperEnvelope.attackMs)}ms`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_DECAY) {
            stepperEnvelope.decayMs = 20 + (data2 / 127) * 1980;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Center Decay: ${Math.round(stepperEnvelope.decayMs)}ms`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_SUSTAIN) {
            stepperEnvelope.sustainLevel = data2 / 127;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Center Sustain: ${Math.round(stepperEnvelope.sustainLevel * 100)}%`;
            return;
          }

          if (data1 === MIDI_CC_STEPPER_1_RELEASE) {
            stepperEnvelope.releaseMs = 20 + (data2 / 127) * 1980;
            updateStepperEnvelopeUi();
            saveState();
            midiStatus.textContent =
              `Center Release: ${Math.round(stepperEnvelope.releaseMs)}ms`;
            return;
          }

          // Speed knob
          if (data1 === MIDI_CC_SPEED) {
            animationSpeed = 1 + (data2 / 127) * 17;
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
              const sequenceIds = sequenceOptions.map((option) => option.value);
              const idx = sequenceIds.indexOf(selectedSequenceId);
              selectedSequenceId = sequenceIds[(idx + 1) % sequenceIds.length];
              updateEditorFromSequence();
              updateAnimationControls();
              saveState();
              if (animationTimerId !== null) startAnimation();
              midiStatus.textContent = `Next: ${selectedSequenceId}`;
              return;
            }
            if (data1 === MIDI_CC_BACK) {
              const sequenceIds = sequenceOptions.map((option) => option.value);
              const idx = sequenceIds.indexOf(selectedSequenceId);
              selectedSequenceId =
                sequenceIds[(idx - 1 + sequenceIds.length) % sequenceIds.length];
              updateEditorFromSequence();
              updateAnimationControls();
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
          rings,
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
          setScriptEditorSnapshot({ error: null });
        } catch (err) {
          setScriptEditorSnapshot({ error: err.message });
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
        rings = DEFAULT_RINGS;
        updateGridControls();
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

        const totalRings = rings;

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

      setDeviceCommandHandler("relay", async (command) => {
        try {
          await sendRelayCommand(command);
          return true;
        } catch (error) {
          console.error(error);
          relayStatusMessage = error?.message || "Relay command failed";
          updateSerialUi(scene ? getMappedRelayNodeIds(scene).length : 0);
          return false;
        }
      });
      setDeviceCommandHandler("stepper", async (command) => {
        try {
          await sendStepperCommand(command);
          return true;
        } catch (error) {
          console.error(error);
          stepperStatusMessage = "Stepper command failed";
          updateStepperUi();
          return false;
        }
      });
      function updateEditorFromSequence() {
        setScriptEditorSnapshot({
          error: null,
          helpOpen: scriptHelpOpen,
          open: scriptEditorOpen,
          script: getScriptForSequence(selectedSequenceId),
        });
      }

      const savedState = loadState();
      if (savedState?.rings !== null) {
        rings = Math.min(Math.max(savedState.rings, MIN_RINGS), MAX_RINGS);
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

      if (savedState?.connections?.length > 0) {
        connections.splice(
          0,
          connections.length,
          ...savedState.connections.map((connection) =>
            createConnection(connection.type, connection),
          ),
        );
      } else {
        connections.push(createConnection("relay", { name: "Relay 1" }));
        connections.push(createConnection("stepper", { name: "Stepper 1" }));
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

      const primaryStepper = getPrimaryStepperConnection();
      if (primaryStepper) {
        primaryStepper.channels[0].positionPercent = stepperPositionPercent;
        primaryStepper.channels[0].travelSteps = stepperTravelSteps;
      }

      if (Number.isFinite(savedState?.centerEnvelopeAttackMs)) {
        stepperEnvelope.attackMs = Math.min(
          Math.max(savedState.centerEnvelopeAttackMs, 20),
          2000,
        );
      }

      if (Number.isFinite(savedState?.centerEnvelopeDecayMs)) {
        stepperEnvelope.decayMs = Math.min(
          Math.max(savedState.centerEnvelopeDecayMs, 20),
          2000,
        );
      }

      if (Number.isFinite(savedState?.centerEnvelopeSustainLevel)) {
        stepperEnvelope.sustainLevel = Math.min(
          Math.max(savedState.centerEnvelopeSustainLevel, 0),
          1,
        );
      }

      if (Number.isFinite(savedState?.centerEnvelopeReleaseMs)) {
        stepperEnvelope.releaseMs = Math.min(
          Math.max(savedState.centerEnvelopeReleaseMs, 20),
          2000,
        );
      }

      stepperEnvelope.originValue = stepperBasePositionPercent;

      updateEditorFromSequence();
      updateGridControls();
      updateAnimationControls();
      updateSpeedReadout();
      updateStepperReadout();
      updateStepperTravelReadout();
      updateStepperEnvelopeUi();
      updatePlayPauseButton();
      updateLoopToggleButton();
      updateMidiUi();
      updateSerialUi(0);
      updateStepperUi();
      render();
      autoConnectRelay();
      autoConnectStepper();
      initMidi();
