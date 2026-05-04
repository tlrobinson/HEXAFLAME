export type NodeType = "center" | "vertex";
export type JetKind = "spoke" | "outline";
export type JetMode = "all" | "spokes" | "outlines";

export interface SceneNode {
  id: string;
  type: NodeType;
  x: number;
  y: number;
  latticeA: number;
  latticeB: number;
  shell: number;
  address: string;
  neighbors: Set<string>;
  controlledJetIds: string[];
}

export interface Jet {
  id: string;
  kind: JetKind;
  color: string;
  controllerId: string;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

export interface Scene {
  size: number;
  allNodes: SceneNode[];
  nodes: SceneNode[];
  jets: Jet[];
}
