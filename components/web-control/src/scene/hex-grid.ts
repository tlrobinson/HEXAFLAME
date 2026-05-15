import type { Jet, JetMode, Scene, SceneNode } from "../types";

const SQRT3 = Math.sqrt(3);
const DIRECTIONS: ReadonlyArray<readonly [number, number]> = [
  [1, 0],
  [1, -1],
  [0, -1],
  [-1, 0],
  [-1, 1],
  [0, 1],
];

const VERTEX_OFFSETS: ReadonlyArray<readonly [number, number]> = [
  [1, 0],
  [0, 1],
  [-1, 1],
  [-1, 0],
  [0, -1],
  [1, -1],
];

function cubeToPixel(q: number, r: number, size: number) {
  return {
    x: size * 1.5 * q,
    y: size * SQRT3 * (r + q / 2),
  };
}

function segmentKey(a: string, b: string) {
  return a < b ? `${a}|${b}` : `${b}|${a}`;
}

function partialSegment(
  fromX: number,
  fromY: number,
  toX: number,
  toY: number,
  fraction: number,
) {
  return {
    x1: fromX,
    y1: fromY,
    x2: fromX + (toX - fromX) * fraction,
    y2: fromY + (toY - fromY) * fraction,
  };
}

function latticeShell(a: number, b: number) {
  return Math.max(Math.abs(a), Math.abs(b), Math.abs(a - b));
}

function centerShell(q: number, r: number) {
  return Math.max(Math.abs(q), Math.abs(r), Math.abs(q + r));
}

function clockwiseIndex(x: number, y: number, startAngle: number) {
  const angle = Math.atan2(y, x);
  return (angle - startAngle + Math.PI * 2) % (Math.PI * 2);
}

function ringCoordinates(radius: number): Array<[number, number]> {
  if (radius === 0) {
    return [[0, 0]];
  }

  const cells: Array<[number, number]> = [];
  let q = DIRECTIONS[4][0] * radius;
  let r = DIRECTIONS[4][1] * radius;

  for (let side = 0; side < DIRECTIONS.length; side += 1) {
    for (let step = 0; step < radius; step += 1) {
      cells.push([q, r]);
      q += DIRECTIONS[side][0];
      r += DIRECTIONS[side][1];
    }
  }

  return cells;
}

function allCoordinates(totalRings: number) {
  const cells: Array<[number, number]> = [];
  for (let radius = 0; radius < totalRings; radius += 1) {
    cells.push(
      ...ringCoordinates(radius)
        .map(([q, r]) => {
          const point = cubeToPixel(q, r, 1);
          return {
            point,
            q,
            r,
          };
        })
        .sort((left, right) => {
          const ringDelta = centerShell(left.q, left.r) - centerShell(right.q, right.r);
          if (ringDelta !== 0) {
            return ringDelta;
          }

          const angleDelta =
            clockwiseIndex(left.point.x, left.point.y, (-5 * Math.PI) / 6) -
            clockwiseIndex(right.point.x, right.point.y, (-5 * Math.PI) / 6);
          if (Math.abs(angleDelta) > 0.0001) {
            return angleDelta;
          }

          return left.q - right.q || left.r - right.r;
        })
        .map(({ q, r }): [number, number] => [q, r]),
    );
  }
  return cells;
}

function hexVertices(centerX: number, centerY: number, size: number) {
  const vertices = [];
  for (let i = 0; i < 6; i += 1) {
    const angle = 60 * i * (Math.PI / 180);
    vertices.push({
      x: centerX + size * Math.cos(angle),
      y: centerY + size * Math.sin(angle),
    });
  }
  return vertices;
}

function localVertexAddressIndex(vertexIndex: number) {
  return [2, 3, 4, 5, 0, 1][vertexIndex] ?? vertexIndex;
}

function buildSceneWithGeometry(
  totalRings: number,
  size: number,
  originX: number,
  originY: number,
  jetMode: JetMode,
): Scene {
  const coords = allCoordinates(totalRings);
  const nodes: SceneNode[] = [];
  const nodeMap = new Map<string, SceneNode>();
  const jets: Jet[] = [];
  const ringHexCounts = new Map<number, number>();

  for (const [q, r] of coords) {
    const point = cubeToPixel(q, r, size);
    const centerX = originX + point.x;
    const centerY = originY + point.y;
    const latticeA = q - r;
    const latticeB = q + 2 * r;
    const ring = centerShell(q, r);
    const hexIndex = ringHexCounts.get(ring) || 0;
    ringHexCounts.set(ring, hexIndex + 1);
    const centerId = `c:${q},${r}`;
    const centerNode: SceneNode = {
      id: centerId,
      type: "center",
      x: centerX,
      y: centerY,
      latticeA,
      latticeB,
      shell: ring,
      address: `C${ring}.${hexIndex}`,
      neighbors: new Set(),
      controlledJetIds: [],
    };
    nodes.push(centerNode);
    nodeMap.set(centerId, centerNode);

    const vertices = hexVertices(centerX, centerY, size).map((vertex, index) => {
      const vertexA = latticeA + VERTEX_OFFSETS[index][0];
      const vertexB = latticeB + VERTEX_OFFSETS[index][1];
      const id = `v:${vertexA},${vertexB}`;
      if (!nodeMap.has(id)) {
        const vertexNode: SceneNode = {
          id,
          type: "vertex",
          x: vertex.x,
          y: vertex.y,
          latticeA: vertexA,
          latticeB: vertexB,
          shell: Math.max(latticeShell(vertexA, vertexB) - 1, 0),
          address: `V${ring}.${hexIndex}.${localVertexAddressIndex(index)}`,
          neighbors: new Set(),
          controlledJetIds: [],
        };
        nodes.push(vertexNode);
        nodeMap.set(id, vertexNode);
      }
      return nodeMap.get(id)!;
    });

    for (const vertexNode of vertices) {
      const spokeId = `s:${centerId}:${vertexNode.id}`;
      centerNode.neighbors.add(vertexNode.id);
      vertexNode.neighbors.add(centerId);
      jets.push({
        id: spokeId,
        kind: "spoke",
        color: "#ff3860",
        controllerId: centerId,
        ...partialSegment(centerX, centerY, vertexNode.x, vertexNode.y, 0.75),
      });
      centerNode.controlledJetIds.push(spokeId);
    }

    for (let i = 0; i < vertices.length; i += 1) {
      const start = vertices[i];
      const end = vertices[(i + 1) % vertices.length];
      const edgeId = segmentKey(start.id, end.id);
      const forwardJetId = `${edgeId}:${start.id}`;
      const backwardJetId = `${edgeId}:${end.id}`;
      start.neighbors.add(end.id);
      end.neighbors.add(start.id);

      if (!start.controlledJetIds.includes(forwardJetId)) {
        start.controlledJetIds.push(forwardJetId);
        jets.push({
          id: forwardJetId,
          kind: "outline",
          color: "#6c5ce7",
          controllerId: start.id,
          ...partialSegment(start.x, start.y, end.x, end.y, 0.75),
        });
      }

      if (!end.controlledJetIds.includes(backwardJetId)) {
        end.controlledJetIds.push(backwardJetId);
        jets.push({
          id: backwardJetId,
          kind: "outline",
          color: "#6c5ce7",
          controllerId: end.id,
          ...partialSegment(end.x, end.y, start.x, start.y, 0.75),
        });
      }
    }
  }

  const includeOutlines = jetMode !== "spokes";
  const includeSpokes = jetMode !== "outlines";
  const filteredNodes = nodes
    .filter((node) => (node.type === "center" ? includeSpokes : includeOutlines))
    .map((node) => ({ ...node }));
  const filteredNodeIds = new Set(filteredNodes.map((node) => node.id));

  for (const node of filteredNodes) {
    node.neighbors = new Set(
      [...node.neighbors].filter((id) => filteredNodeIds.has(id)),
    );
  }

  const filteredJets = jets.filter((jet) => filteredNodeIds.has(jet.controllerId));

  return {
    size,
    allNodes: nodes,
    nodes: filteredNodes,
    jets: filteredJets,
  };
}

export function buildScene(
  totalRings: number,
  width: number,
  height: number,
  jetMode: JetMode,
): Scene {
  const maxRadius = Math.max(totalRings - 1, 0);
  const maxX = 1.5 * maxRadius + 1;
  const maxY = SQRT3 * (maxRadius + 1);
  const size = Math.min(width / (2 * maxX), height / (2 * maxY)) * 0.92;
  return buildSceneWithGeometry(totalRings, size, width / 2, height / 2, jetMode);
}

export function buildSceneWithHexRadius(
  totalRings: number,
  radius: number,
  originX = 0,
  originY = 0,
  jetMode: JetMode = "all",
): Scene {
  return buildSceneWithGeometry(totalRings, radius, originX, originY, jetMode);
}

export function getCenterNode(currentScene: Scene) {
  return (
    currentScene.allNodes.find(
      (node) => node.type === "center" && node.id === "c:0,0",
    ) || null
  );
}

export function buildDistanceMap(currentScene: Scene) {
  const centerNode = getCenterNode(currentScene);
  if (!centerNode) {
    return new Map<string, number>();
  }

  const distanceMap = new Map<string, number>([[centerNode.id, 0]]);
  const queue: SceneNode[] = [centerNode];

  while (queue.length > 0) {
    const node = queue.shift();
    if (!node) {
      continue;
    }
    const distance = distanceMap.get(node.id) ?? 0;
    for (const neighborId of node.neighbors) {
      if (distanceMap.has(neighborId)) {
        continue;
      }

      const neighbor = currentScene.allNodes.find(
        (candidate) => candidate.id === neighborId,
      );
      if (!neighbor) {
        continue;
      }

      distanceMap.set(neighborId, distance + 1);
      queue.push(neighbor);
    }
  }

  return distanceMap;
}
