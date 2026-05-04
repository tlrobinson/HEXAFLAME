import type { Jet, Scene, SceneNode } from "../types";

export type LabelMode = "address" | "distance" | "channel" | "none";

export function drawJet(
  context: CanvasRenderingContext2D,
  jet: Jet,
  active: boolean,
  hovered: boolean,
) {
  context.beginPath();
  context.moveTo(jet.x1, jet.y1);
  context.lineTo(jet.x2, jet.y2);
  context.lineCap = "round";
  context.strokeStyle = jet.color;
  context.lineWidth = hovered ? 3 : 2;
  context.globalAlpha = active ? 1 : 0.12;
  context.stroke();
  context.globalAlpha = 1;
}

export function drawStepperGlow(
  context: CanvasRenderingContext2D,
  currentScene: Scene,
  positionPercent: number,
) {
  const intensity = Math.min(Math.max(positionPercent / 100, 0), 1);
  if (intensity <= 0.001) {
    return;
  }

  const centerNode = currentScene.nodes.find(
    (node) => node.type === "center" && node.id === "c:0,0",
  );
  if (!centerNode) {
    return;
  }

  const spokeJets = currentScene.jets.filter(
    (jet) => jet.kind === "spoke" && jet.controllerId === centerNode.id,
  );
  if (spokeJets.length === 0) {
    return;
  }

  const outerWidth = 3 + intensity * 16;
  const innerWidth = 1.5 + intensity * 6;
  const glowAlpha = 0.14 + intensity * 0.42;
  const coreAlpha = 0.35 + intensity * 0.55;
  const centerRadius = 6 + intensity * 24;
  const baseColor = spokeJets[0].color || "#ff3860";

  context.save();
  context.globalCompositeOperation = "screen";

  for (const jet of spokeJets) {
    context.beginPath();
    context.moveTo(jet.x1, jet.y1);
    context.lineTo(jet.x2, jet.y2);
    context.lineCap = "round";
    context.lineWidth = outerWidth;
    context.strokeStyle = baseColor;
    context.globalAlpha = glowAlpha;
    context.shadowColor = baseColor;
    context.shadowBlur = 10 + intensity * 28;
    context.stroke();

    context.beginPath();
    context.moveTo(jet.x1, jet.y1);
    context.lineTo(jet.x2, jet.y2);
    context.lineCap = "round";
    context.lineWidth = innerWidth;
    context.strokeStyle = baseColor;
    context.globalAlpha = coreAlpha;
    context.shadowBlur = 0;
    context.stroke();
  }

  const radial = context.createRadialGradient(
    centerNode.x,
    centerNode.y,
    0,
    centerNode.x,
    centerNode.y,
    centerRadius,
  );
  radial.addColorStop(0, "rgba(255, 255, 255, 0.28)");
  radial.addColorStop(0.28, baseColor);
  radial.addColorStop(1, "rgba(0, 0, 0, 0)");
  context.beginPath();
  context.arc(centerNode.x, centerNode.y, centerRadius, 0, Math.PI * 2);
  context.fillStyle = radial;
  context.globalAlpha = 0.18 + intensity * 0.28;
  context.fill();

  context.restore();
}

export function drawOutlineGlow(
  context: CanvasRenderingContext2D,
  currentScene: Scene,
  activeNodeIds: Set<string>,
) {
  const outlineJets = currentScene.jets.filter(
    (jet) => jet.kind === "outline" && activeNodeIds.has(jet.controllerId),
  );
  if (outlineJets.length === 0) {
    return;
  }

  context.save();
  context.globalCompositeOperation = "screen";

  for (const jet of outlineJets) {
    const baseColor = jet.color || "#6c5ce7";

    context.beginPath();
    context.moveTo(jet.x1, jet.y1);
    context.lineTo(jet.x2, jet.y2);
    context.lineCap = "round";
    context.lineWidth = 15;
    context.strokeStyle = baseColor;
    context.globalAlpha = 0.5;
    context.shadowColor = baseColor;
    context.shadowBlur = 30;
    context.stroke();

    context.beginPath();
    context.moveTo(jet.x1, jet.y1);
    context.lineTo(jet.x2, jet.y2);
    context.lineCap = "round";
    context.lineWidth = 7.5;
    context.strokeStyle = baseColor;
    context.globalAlpha = 0.9;
    context.shadowBlur = 0;
    context.stroke();
  }

  context.restore();
}

export function drawHoverNode(
  context: CanvasRenderingContext2D,
  node: SceneNode | null,
) {
  if (!node) {
    return;
  }

  context.beginPath();
  context.arc(node.x, node.y, 5, 0, Math.PI * 2);
  context.fillStyle = "rgba(200, 196, 216, 0.25)";
  context.fill();
}

export function drawAddressLabels(
  context: CanvasRenderingContext2D,
  nodes: SceneNode[],
  labelMode: LabelMode,
) {
  if (labelMode !== "address") {
    return;
  }

  context.font = "11px Avenir Next, Segoe UI, sans-serif";
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (const node of nodes) {
    const yOffset = node.type === "center" ? -14 : 13;
    context.fillStyle = "rgba(200, 196, 216, 0.6)";
    context.fillText(node.address, node.x, node.y + yOffset);
  }
}

export function drawDistanceLabels(
  context: CanvasRenderingContext2D,
  nodes: SceneNode[],
  distanceMap: Map<string, number>,
  labelMode: LabelMode,
) {
  if (labelMode !== "distance") {
    return;
  }

  context.font = "10px Avenir Next, Segoe UI, sans-serif";
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (const node of nodes) {
    context.fillStyle = "rgba(28, 24, 50, 0.92)";
    context.beginPath();
    context.arc(node.x, node.y, 6, 0, Math.PI * 2);
    context.fill();
    context.fillStyle = "rgba(200, 196, 216, 0.85)";
    context.fillText(String(distanceMap.get(node.id) ?? 0), node.x, node.y + 0.5);
  }
}

export function drawChannelLabels(
  context: CanvasRenderingContext2D,
  currentScene: Scene,
  mappedNodeIds: string[],
  labelMode: LabelMode,
) {
  if (labelMode !== "channel") {
    return;
  }

  const channelMap = new Map<string, number>();
  for (let i = 0; i < mappedNodeIds.length; i++) {
    channelMap.set(mappedNodeIds[i], i + 1);
  }

  context.font = "bold 10px Avenir Next, Segoe UI, sans-serif";
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (const node of currentScene.nodes) {
    const channel = channelMap.get(node.id);
    if (channel === undefined) {
      continue;
    }

    context.fillStyle = "rgba(0, 229, 255, 0.85)";
    context.beginPath();
    context.arc(node.x, node.y, 8, 0, Math.PI * 2);
    context.fill();
    context.fillStyle = "#0a0816";
    context.fillText(String(channel), node.x, node.y + 0.5);
  }
}
