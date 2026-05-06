import "./layout.css";
import { buildSceneWithHexRadius } from "./scene/hex-grid";
import type { SceneNode } from "./types";

const VALVE_SPACING_MM = 65;
const VALVE_STRAIGHT_LENGTH_MM = 150;
const MIN_BEND_RADIUS_MM = 25;
const ROUTE_CLEARANCE_MM = 90;
const MAX_BEND_ANGLE_RAD = (150 * Math.PI) / 180;
const FIELD_RINGS = 2;
const TAU = Math.PI * 2;

type ValveSideMode = "inner" | "outer" | "both";
type ValveOutletSide = "inner" | "outer";

interface Point {
  x: number;
  y: number;
}

interface LayoutJet extends Point {
  id: string;
  label: string;
  type: SceneNode["type"];
}

interface Valve extends Point {
  id: string;
  manifoldIndex: number;
  valveIndex: number;
  angle: number;
  normal: Point;
}

interface ValveOutlet extends Point {
  id: string;
  valve: Valve;
  side: ValveOutletSide;
  normal: Point;
}

interface Assignment {
  jet: LayoutJet;
  valve: Valve;
  outlet: ValveOutlet;
  route: Point[];
  lengthMm: number;
}

const canvas = document.getElementById("layout-canvas") as HTMLCanvasElement;
const context = canvas.getContext("2d");

const hexRadiusInput = document.getElementById("hex-radius") as HTMLInputElement;
const manifoldCountInput = document.getElementById(
  "manifold-count",
) as HTMLInputElement;
const jetsPerManifoldInput = document.getElementById(
  "jets-per-manifold",
) as HTMLInputElement;
const manifoldRadiusInput = document.getElementById(
  "manifold-radius",
) as HTMLInputElement;
const valveSideInputs = [
  ...document.querySelectorAll<HTMLInputElement>('input[name="valve-side"]'),
];

const hexRadiusReadout = document.getElementById("hex-radius-readout")!;
const manifoldCountReadout = document.getElementById("manifold-count-readout")!;
const jetsPerManifoldReadout = document.getElementById(
  "jets-per-manifold-readout",
)!;
const manifoldRadiusReadout = document.getElementById(
  "manifold-radius-readout",
)!;
const jetCountText = document.getElementById("jet-count")!;
const valveCountText = document.getElementById("valve-count")!;
const assignedCountText = document.getElementById("assigned-count")!;
const capacityStatusText = document.getElementById("capacity-status")!;
const tubeTotalText = document.getElementById("tube-total")!;
const tubeStdDevText = document.getElementById("tube-stddev")!;
const tubeHistogram = document.getElementById("tube-histogram")!;

function getCssVar(name: string) {
  return getComputedStyle(document.documentElement)
    .getPropertyValue(name)
    .trim();
}

function getControls() {
  return {
    hexRadius: Number(hexRadiusInput.value),
    manifoldCount: Number(manifoldCountInput.value),
    jetsPerManifold: Number(jetsPerManifoldInput.value),
    manifoldRadius: Number(manifoldRadiusInput.value),
    valveSide: (valveSideInputs.find((input) => input.checked)?.value ||
      "both") as ValveSideMode,
  };
}

function clampToInput(input: HTMLInputElement, value: number) {
  const min = Number(input.min);
  const max = Number(input.max);
  return Math.min(Math.max(Math.round(value), min), max);
}

function getCurrentJetCount() {
  return buildJets(Number(hexRadiusInput.value)).length;
}

function syncJetsPerManifoldToManifoldCount() {
  const manifoldCount = Math.max(1, Number(manifoldCountInput.value));
  const requiredJetsPerManifold = Math.ceil(getCurrentJetCount() / manifoldCount);
  jetsPerManifoldInput.value = String(
    clampToInput(jetsPerManifoldInput, requiredJetsPerManifold),
  );
}

function syncManifoldCountToJetsPerManifold() {
  const jetsPerManifold = Math.max(1, Number(jetsPerManifoldInput.value));
  const requiredManifolds = Math.ceil(getCurrentJetCount() / jetsPerManifold);
  manifoldCountInput.value = String(
    clampToInput(manifoldCountInput, requiredManifolds),
  );
}

function sortByAngle<T extends Point>(points: T[]) {
  return [...points].sort((left, right) => {
    const leftAngle = Math.atan2(left.y, left.x);
    const rightAngle = Math.atan2(right.y, right.x);
    return leftAngle - rightAngle || Math.hypot(left.x, left.y) - Math.hypot(right.x, right.y);
  });
}

function buildJets(hexRadius: number): LayoutJet[] {
  const scene = buildSceneWithHexRadius(FIELD_RINGS, hexRadius, 0, 0, "all");
  return sortByAngle(scene.nodes).map((node, index) => ({
    id: node.id,
    label: `${node.type === "center" ? "C" : "V"}${index + 1}`,
    type: node.type,
    x: node.x,
    y: node.y,
  }));
}

function buildValves(manifoldCount: number, jetsPerManifold: number, radius: number) {
  const valves: Valve[] = [];

  for (let manifoldIndex = 0; manifoldIndex < manifoldCount; manifoldIndex += 1) {
    const angle = -Math.PI / 2 + (TAU * manifoldIndex) / manifoldCount;
    const anchor = {
      x: Math.cos(angle) * radius,
      y: Math.sin(angle) * radius,
    };
    const tangent = {
      x: -Math.sin(angle),
      y: Math.cos(angle),
    };
    const normal = {
      x: Math.cos(angle),
      y: Math.sin(angle),
    };
    const offsetStart = -((jetsPerManifold - 1) * VALVE_SPACING_MM) / 2;

    for (let valveIndex = 0; valveIndex < jetsPerManifold; valveIndex += 1) {
      const offset = offsetStart + valveIndex * VALVE_SPACING_MM;
      valves.push({
        id: `m${manifoldIndex + 1}:v${valveIndex + 1}`,
        manifoldIndex,
        valveIndex,
        angle,
        normal,
        x: anchor.x + tangent.x * offset,
        y: anchor.y + tangent.y * offset,
      });
    }
  }

  return valves;
}

function buildValveOutlets(valves: Valve[], sideMode: ValveSideMode) {
  const sides: ValveOutletSide[] =
    sideMode === "both" ? ["inner", "outer"] : [sideMode];

  return valves.flatMap((valve) =>
    sides.map((side) => {
      const sideSign = side === "outer" ? 1 : -1;
      const normal = {
        x: valve.normal.x * sideSign,
        y: valve.normal.y * sideSign,
      };
      return {
        id: `${valve.id}:${side}`,
        valve,
        side,
        normal,
        x: valve.x + normal.x * (VALVE_STRAIGHT_LENGTH_MM + MIN_BEND_RADIUS_MM),
        y: valve.y + normal.y * (VALVE_STRAIGHT_LENGTH_MM + MIN_BEND_RADIUS_MM),
      };
    }),
  );
}

function dot(left: Point, right: Point) {
  return left.x * right.x + left.y * right.y;
}

function distance(left: Point, right: Point) {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

function scalePoint(point: Point, scale: number) {
  return {
    x: point.x * scale,
    y: point.y * scale,
  };
}

function addPoints(left: Point, right: Point) {
  return {
    x: left.x + right.x,
    y: left.y + right.y,
  };
}

function subtractPoints(left: Point, right: Point) {
  return {
    x: left.x - right.x,
    y: left.y - right.y,
  };
}

function normalize(point: Point) {
  const length = Math.hypot(point.x, point.y);
  if (length <= 0.0001) {
    return { x: 0, y: 0 };
  }

  return {
    x: point.x / length,
    y: point.y / length,
  };
}

function clamp(value: number, min: number, max: number) {
  return Math.min(Math.max(value, min), max);
}

function removeNearDuplicatePoints(points: Point[]) {
  const deduped: Point[] = [];

  for (const point of points) {
    const previous = deduped[deduped.length - 1];
    if (!previous || distance(previous, point) > 0.5) {
      deduped.push(point);
    }
  }

  return deduped;
}

function buildTubeRoute(jet: LayoutJet, outlet: ValveOutlet) {
  const jetDelta = subtractPoints(jet, outlet);
  const localJetV = dot(jetDelta, outlet.normal);
  const tangentBase = {
    x: -outlet.normal.y,
    y: outlet.normal.x,
  };
  const tangentTowardJet =
    dot(jetDelta, tangentBase) >= 0 ? tangentBase : scalePoint(tangentBase, -1);
  const normalOffsets = [
    0,
    ROUTE_CLEARANCE_MM * 0.45,
    -ROUTE_CLEARANCE_MM * 0.45,
    ROUTE_CLEARANCE_MM,
    -ROUTE_CLEARANCE_MM,
  ];
  const tangentOffsets = [
    ROUTE_CLEARANCE_MM,
    ROUTE_CLEARANCE_MM * 1.6,
    ROUTE_CLEARANCE_MM * 2.3,
  ];
  const directRoute = removeNearDuplicatePoints([jet, outlet, outlet.valve]);
  const routes = [directRoute];

  for (const tangentScale of tangentOffsets) {
    for (const normalOffset of normalOffsets) {
      const approach = addPoints(
        outlet,
        addPoints(
          scalePoint(tangentTowardJet, tangentScale),
          scalePoint(outlet.normal, normalOffset),
        ),
      );
      const routeStart =
        Math.abs(localJetV) < ROUTE_CLEARANCE_MM
          ? addPoints(
              jet,
              scalePoint(
                outlet.normal,
                localJetV >= 0 ? ROUTE_CLEARANCE_MM : -ROUTE_CLEARANCE_MM,
              ),
            )
          : jet;
      routes.push(
        removeNearDuplicatePoints([
          jet,
          routeStart,
          approach,
          outlet,
          outlet.valve,
        ]),
      );
    }
  }

  return routes
    .map((route) => ({
      route,
      length: getRoundedRouteLength(route),
      sharpness: getRouteSharpness(route),
    }))
    .filter((candidate) => candidate.sharpness <= MAX_BEND_ANGLE_RAD)
    .sort((left, right) => left.length - right.length)[0]?.route ||
    routes
      .map((route) => ({
        route,
        length: getRoundedRouteLength(route),
        sharpness: getRouteSharpness(route),
      }))
      .sort(
        (left, right) =>
          left.sharpness - right.sharpness || left.length - right.length,
      )[0].route;
}

function getRouteSharpness(points: Point[]) {
  let sharpest = 0;

  for (let index = 1; index < points.length - 1; index += 1) {
    const incomingLength = distance(points[index - 1], points[index]);
    const outgoingLength = distance(points[index], points[index + 1]);
    if (incomingLength <= 0.001 || outgoingLength <= 0.001) {
      continue;
    }

    const incoming = normalize(subtractPoints(points[index], points[index - 1]));
    const outgoing = normalize(subtractPoints(points[index + 1], points[index]));
    sharpest = Math.max(
      sharpest,
      Math.acos(clamp(dot(incoming, outgoing), -1, 1)),
    );
  }

  return sharpest;
}

function getCornerFillet(
  previous: Point,
  corner: Point,
  next: Point,
  radius: number,
) {
  const incomingLength = distance(previous, corner);
  const outgoingLength = distance(corner, next);
  if (incomingLength <= 0.001 || outgoingLength <= 0.001) {
    return null;
  }

  const incoming = normalize(subtractPoints(corner, previous));
  const outgoing = normalize(subtractPoints(next, corner));
  const angle = Math.acos(clamp(dot(incoming, outgoing), -1, 1));
  if (angle < 0.001 || Math.abs(Math.PI - angle) < 0.001) {
    return null;
  }

  const desiredTangentDistance = radius * Math.tan(angle / 2);
  if (!Number.isFinite(desiredTangentDistance)) {
    return null;
  }

  const tangentDistance = Math.min(
    desiredTangentDistance,
    incomingLength / 2,
    outgoingLength / 2,
  );
  if (tangentDistance <= 0.001) {
    return null;
  }

  return {
    angle,
    radius: tangentDistance / Math.tan(angle / 2),
    start: subtractPoints(corner, scalePoint(incoming, tangentDistance)),
    end: addPoints(corner, scalePoint(outgoing, tangentDistance)),
  };
}

function getRoundedRouteLength(points: Point[], radius = MIN_BEND_RADIUS_MM) {
  if (points.length < 2) {
    return 0;
  }

  let current = points[0];
  let length = 0;

  for (let index = 1; index < points.length - 1; index += 1) {
    const fillet = getCornerFillet(
      points[index - 1],
      points[index],
      points[index + 1],
      radius,
    );
    if (!fillet) {
      length += distance(current, points[index]);
      current = points[index];
      continue;
    }

    length += distance(current, fillet.start);
    length += fillet.radius * fillet.angle;
    current = fillet.end;
  }

  length += distance(current, points[points.length - 1]);
  return length;
}

function buildAssignments(
  jets: LayoutJet[],
  valves: Valve[],
  sideMode: ValveSideMode,
): Assignment[] {
  const outlets = buildValveOutlets(valves, sideMode);
  const candidates = jets
    .flatMap((jet) =>
      outlets.map((outlet) => {
        const route = buildTubeRoute(jet, outlet);
        return {
          jet,
          valve: outlet.valve,
          outlet,
          route,
          length: getRoundedRouteLength(route),
        };
      }),
    )
    .sort((left, right) => left.length - right.length);
  const usedJetIds = new Set<string>();
  const usedValveIds = new Set<string>();
  const assignments: Assignment[] = [];

  for (const candidate of candidates) {
    if (
      usedJetIds.has(candidate.jet.id) ||
      usedValveIds.has(candidate.valve.id)
    ) {
      continue;
    }

    assignments.push({
      jet: candidate.jet,
      valve: candidate.valve,
      outlet: candidate.outlet,
      route: candidate.route,
      lengthMm: candidate.length,
    });
    usedJetIds.add(candidate.jet.id);
    usedValveIds.add(candidate.valve.id);

    if (assignments.length >= Math.min(jets.length, valves.length)) {
      break;
    }
  }

  return assignments;
}

function getTubeLengthMm(assignment: Assignment) {
  return assignment.lengthMm;
}

function buildHistogramBuckets(lengths: number[]) {
  if (lengths.length === 0) {
    return [];
  }

  const min = Math.min(...lengths);
  const max = Math.max(...lengths);
  const bucketCount = Math.min(10, Math.max(4, Math.ceil(Math.sqrt(lengths.length))));
  const bucketSize = Math.max(1, (max - min) / bucketCount);
  const buckets = Array.from({ length: bucketCount }, (_, index) => ({
    min: min + bucketSize * index,
    max: index === bucketCount - 1 ? max : min + bucketSize * (index + 1),
    count: 0,
  }));

  for (const length of lengths) {
    const index = Math.min(
      bucketCount - 1,
      Math.floor((length - min) / bucketSize),
    );
    buckets[index].count += 1;
  }

  return buckets;
}

function renderTubeStats(assignments: Assignment[]) {
  const lengths = assignments.map(getTubeLengthMm);
  const total = lengths.reduce((sum, length) => sum + length, 0);
  const mean = lengths.length > 0 ? total / lengths.length : 0;
  const variance =
    lengths.length > 0
      ? lengths.reduce((sum, length) => sum + (length - mean) ** 2, 0) /
        lengths.length
      : 0;
  const stdDev = Math.sqrt(variance);
  const buckets = buildHistogramBuckets(lengths);
  const maxCount = Math.max(1, ...buckets.map((bucket) => bucket.count));

  tubeTotalText.textContent = `${(total / 1000).toFixed(2)} m`;
  tubeStdDevText.textContent = `${Math.round(stdDev)} mm`;
  tubeHistogram.replaceChildren(
    ...buckets.map((bucket) => {
      const row = document.createElement("div");
      row.className = "histogram-row";

      const label = document.createElement("span");
      label.className = "histogram-label";
      label.textContent = `${Math.round(bucket.min)}-${Math.round(bucket.max)}mm`;

      const track = document.createElement("span");
      track.className = "histogram-track";

      const bar = document.createElement("span");
      bar.className = "histogram-bar";
      bar.style.width = `${Math.max(4, (bucket.count / maxCount) * 100)}%`;
      track.append(bar);

      const count = document.createElement("span");
      count.className = "histogram-count";
      count.textContent = String(bucket.count);

      row.append(label, track, count);
      return row;
    }),
  );
}

function getBounds(points: Point[]) {
  const padding = 120;
  const xs = points.map((point) => point.x);
  const ys = points.map((point) => point.y);
  return {
    minX: Math.min(...xs) - padding,
    maxX: Math.max(...xs) + padding,
    minY: Math.min(...ys) - padding,
    maxY: Math.max(...ys) + padding,
  };
}

function resizeCanvas() {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(rect.width * dpr));
  canvas.height = Math.max(1, Math.round(rect.height * dpr));
  context?.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function worldToScreen(bounds: ReturnType<typeof getBounds>) {
  const rect = canvas.getBoundingClientRect();
  const worldWidth = bounds.maxX - bounds.minX;
  const worldHeight = bounds.maxY - bounds.minY;
  const scale = Math.min(rect.width / worldWidth, rect.height / worldHeight);
  const offsetX = (rect.width - worldWidth * scale) / 2 - bounds.minX * scale;
  const offsetY = (rect.height - worldHeight * scale) / 2 - bounds.minY * scale;

  return (point: Point) => ({
    x: point.x * scale + offsetX,
    y: point.y * scale + offsetY,
    scale,
  });
}

function drawHexField(hexRadius: number, toScreen: (point: Point) => Point) {
  if (!context) {
    return;
  }

  const scene = buildSceneWithHexRadius(FIELD_RINGS, hexRadius, 0, 0, "all");
  const nodeById = new Map(scene.allNodes.map((node) => [node.id, node]));
  context.save();
  context.lineWidth = 1.4;
  context.strokeStyle = "rgba(138, 197, 180, 0.28)";

  for (const center of scene.allNodes.filter((node) => node.type === "center")) {
    const vertices = [...center.neighbors]
      .map((id) => nodeById.get(id))
      .filter((node): node is SceneNode => Boolean(node) && node.type === "vertex")
      .sort(
        (left, right) =>
          Math.atan2(left.y - center.y, left.x - center.x) -
          Math.atan2(right.y - center.y, right.x - center.x),
      );
    if (vertices.length < 6) {
      continue;
    }

    const start = toScreen(vertices[0]);
    context.beginPath();
    context.moveTo(start.x, start.y);
    for (const vertex of vertices.slice(1)) {
      const point = toScreen(vertex);
      context.lineTo(point.x, point.y);
    }
    context.closePath();
    context.stroke();
  }

  context.lineWidth = 0.8;
  context.strokeStyle = "rgba(138, 197, 180, 0.14)";
  for (const jet of scene.jets) {
    const start = toScreen({ x: jet.x1, y: jet.y1 });
    const end = toScreen({ x: jet.x2, y: jet.y2 });
    context.beginPath();
    context.moveTo(start.x, start.y);
    context.lineTo(end.x, end.y);
    context.stroke();
  }

  context.restore();
}

function getScreenScale(toScreen: (point: Point) => Point) {
  const origin = toScreen({ x: 0, y: 0 });
  const unit = toScreen({ x: 1, y: 0 });
  return Math.abs(unit.x - origin.x);
}

function drawRoundedPolyline(points: Point[], radius: number) {
  if (!context || points.length < 2) {
    return;
  }

  context.beginPath();
  context.moveTo(points[0].x, points[0].y);

  for (let index = 1; index < points.length - 1; index += 1) {
    const fillet = getCornerFillet(
      points[index - 1],
      points[index],
      points[index + 1],
      radius,
    );
    if (!fillet) {
      context.lineTo(points[index].x, points[index].y);
      continue;
    }

    context.lineTo(fillet.start.x, fillet.start.y);
    context.quadraticCurveTo(
      points[index].x,
      points[index].y,
      fillet.end.x,
      fillet.end.y,
    );
  }

  const end = points[points.length - 1];
  context.lineTo(end.x, end.y);
  context.stroke();
}

function drawTube(assignment: Assignment, toScreen: (point: Point) => Point) {
  if (!context) {
    return;
  }

  drawRoundedPolyline(
    assignment.route.map((point) => toScreen(point)),
    MIN_BEND_RADIUS_MM * getScreenScale(toScreen),
  );
}

function drawPoint(point: Point, radius: number, fill: string, stroke: string) {
  if (!context) {
    return;
  }

  context.beginPath();
  context.arc(point.x, point.y, radius, 0, TAU);
  context.fillStyle = fill;
  context.fill();
  context.lineWidth = 1.5;
  context.strokeStyle = stroke;
  context.stroke();
}

function drawLabel(text: string, point: Point, offsetY = -10) {
  if (!context) {
    return;
  }

  context.font = "10px JetBrains Mono, monospace";
  context.textAlign = "center";
  context.textBaseline = "middle";
  context.fillStyle = "rgba(244, 255, 248, 0.78)";
  context.fillText(text, point.x, point.y + offsetY);
}

function render() {
  if (!context) {
    return;
  }

  resizeCanvas();
  const {
    hexRadius,
    manifoldCount,
    jetsPerManifold,
    manifoldRadius,
    valveSide,
  } = getControls();
  const jets = buildJets(hexRadius);
  const valves = buildValves(manifoldCount, jetsPerManifold, manifoldRadius);
  const outlets = buildValveOutlets(valves, valveSide);
  const assignments = buildAssignments(jets, valves, valveSide);
  const assignedJetIds = new Set(assignments.map((assignment) => assignment.jet.id));
  const assignedValveIds = new Set(assignments.map((assignment) => assignment.valve.id));
  const routePoints = assignments.flatMap((assignment) => assignment.route);
  const bounds = getBounds([
    ...jets,
    ...valves,
    ...outlets,
    ...routePoints,
    { x: 0, y: 0 },
  ]);
  const toScreen = worldToScreen(bounds);
  const rect = canvas.getBoundingClientRect();
  const jetColor = getCssVar("--jet");
  const valveColor = getCssVar("--valve");
  const tubeColor = getCssVar("--tube");
  const warnColor = getCssVar("--warn");

  context.clearRect(0, 0, rect.width, rect.height);
  context.fillStyle = "rgba(7, 16, 17, 0.86)";
  context.fillRect(0, 0, rect.width, rect.height);

  const origin = toScreen({ x: 0, y: 0 });
  const manifoldEdge = toScreen({ x: manifoldRadius, y: 0 });
  context.save();
  context.setLineDash([6, 8]);
  context.strokeStyle = "rgba(69, 224, 168, 0.22)";
  context.lineWidth = 1;
  context.beginPath();
  context.arc(origin.x, origin.y, Math.abs(manifoldEdge.x - origin.x), 0, TAU);
  context.stroke();
  context.restore();

  drawHexField(hexRadius, toScreen);

  context.save();
  context.lineWidth = 1.5;
  context.strokeStyle = "rgba(97, 195, 255, 0.38)";
  for (const assignment of assignments) {
    drawTube(assignment, toScreen);
  }
  context.strokeStyle = tubeColor;
  context.globalAlpha = 0.28;
  context.lineWidth = 4;
  for (const assignment of assignments) {
    drawTube(assignment, toScreen);
  }
  context.restore();

  for (const valve of valves) {
    const point = toScreen(valve);
    const assigned = assignedValveIds.has(valve.id);
    drawPoint(
      point,
      assigned ? 5 : 3.5,
      assigned ? valveColor : "rgba(127, 154, 144, 0.36)",
      "rgba(244, 255, 248, 0.42)",
    );
    if (valve.valveIndex === 0) {
      drawLabel(`M${valve.manifoldIndex + 1}`, point, -15);
    }
  }

  for (const jet of jets) {
    const point = toScreen(jet);
    const assigned = assignedJetIds.has(jet.id);
    drawPoint(
      point,
      jet.type === "center" ? 6 : 4.6,
      assigned ? jetColor : warnColor,
      "rgba(7, 16, 17, 0.85)",
    );
  }

  hexRadiusReadout.textContent = `${hexRadius} mm`;
  manifoldCountReadout.textContent = String(manifoldCount);
  jetsPerManifoldReadout.textContent = String(jetsPerManifold);
  manifoldRadiusReadout.textContent = `${manifoldRadius} mm`;
  jetCountText.textContent = String(jets.length);
  valveCountText.textContent = String(valves.length);
  assignedCountText.textContent = String(assignments.length);
  renderTubeStats(assignments);

  const spare = valves.length - jets.length;
  capacityStatusText.textContent =
    spare >= 0 ? `${spare} spare` : `${Math.abs(spare)} short`;
  capacityStatusText.style.color = spare >= 0 ? "var(--bright)" : "var(--warn)";
}

hexRadiusInput.addEventListener("input", () => {
  syncJetsPerManifoldToManifoldCount();
  render();
});

manifoldCountInput.addEventListener("input", () => {
  syncJetsPerManifoldToManifoldCount();
  render();
});

jetsPerManifoldInput.addEventListener("input", () => {
  syncManifoldCountToJetsPerManifold();
  render();
});

for (const input of [manifoldRadiusInput, ...valveSideInputs]) {
  input.addEventListener("input", render);
}

window.addEventListener("resize", render);
syncJetsPerManifoldToManifoldCount();
render();
