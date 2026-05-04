export type AdsrPhase = "idle" | "attack" | "decay" | "sustain" | "release";

export interface AdsrEnvelope {
  active: boolean;
  noteHeld: boolean;
  phase: AdsrPhase;
  attackMs: number;
  decayMs: number;
  sustainLevel: number;
  releaseMs: number;
  originValue: number;
  targetValue: number;
  velocityScale: number;
  currentLevel: number;
  phaseStartMs: number;
  phaseStartLevel: number;
}

export interface AdsrParameters {
  attackMs: number;
  decayMs: number;
  sustainLevel: number;
  releaseMs: number;
}

export function createAdsrEnvelope(
  parameters: Partial<AdsrParameters> = {},
): AdsrEnvelope {
  return {
    active: false,
    noteHeld: false,
    phase: "idle",
    attackMs: parameters.attackMs ?? 180,
    decayMs: parameters.decayMs ?? 220,
    sustainLevel: parameters.sustainLevel ?? 0.55,
    releaseMs: parameters.releaseMs ?? 320,
    originValue: 0,
    targetValue: 1,
    velocityScale: 1,
    currentLevel: 0,
    phaseStartMs: 0,
    phaseStartLevel: 0,
  };
}

export function setAdsrPhase(
  envelope: AdsrEnvelope,
  phase: AdsrPhase,
  now: number,
) {
  envelope.phase = phase;
  envelope.phaseStartMs = now;
  envelope.phaseStartLevel = envelope.currentLevel;
}

export function triggerAdsrEnvelope(
  envelope: AdsrEnvelope,
  {
    velocity = 127,
    originValue = envelope.originValue,
    targetValue = envelope.targetValue,
    now,
  }: {
    velocity?: number;
    originValue?: number;
    targetValue?: number;
    now: number;
  },
) {
  envelope.originValue = originValue;
  envelope.targetValue = targetValue;
  envelope.velocityScale = Math.min(Math.max(velocity / 127, 0), 1);
  envelope.noteHeld = true;

  if (!envelope.active) {
    envelope.active = true;
    envelope.currentLevel = 0;
    envelope.phaseStartLevel = 0;
  }

  setAdsrPhase(envelope, "attack", now);
}

export function releaseAdsrEnvelope(envelope: AdsrEnvelope, now: number) {
  if (!envelope.active) {
    return false;
  }

  envelope.noteHeld = false;
  if (envelope.phase !== "release") {
    setAdsrPhase(envelope, "release", now);
  }

  return true;
}

export function stopAdsrEnvelope(envelope: AdsrEnvelope) {
  envelope.active = false;
  envelope.noteHeld = false;
  envelope.phase = "idle";
  envelope.velocityScale = 1;
  envelope.currentLevel = 0;
  envelope.phaseStartLevel = 0;
}

export function tickAdsrEnvelope(envelope: AdsrEnvelope, now: number) {
  if (!envelope.active) {
    return { active: false, stopped: false };
  }

  const elapsed = now - envelope.phaseStartMs;

  if (envelope.phase === "attack") {
    const progress =
      envelope.attackMs <= 0 ? 1 : Math.min(elapsed / envelope.attackMs, 1);
    envelope.currentLevel =
      envelope.phaseStartLevel + (1 - envelope.phaseStartLevel) * progress;
    if (progress >= 1) {
      envelope.currentLevel = 1;
      setAdsrPhase(envelope, "decay", now);
    }
  } else if (envelope.phase === "decay") {
    const progress =
      envelope.decayMs <= 0 ? 1 : Math.min(elapsed / envelope.decayMs, 1);
    envelope.currentLevel = 1 + (envelope.sustainLevel - 1) * progress;
    if (progress >= 1) {
      envelope.currentLevel = envelope.sustainLevel;
      setAdsrPhase(envelope, envelope.noteHeld ? "sustain" : "release", now);
    }
  } else if (envelope.phase === "sustain") {
    envelope.currentLevel = envelope.sustainLevel;
    if (!envelope.noteHeld) {
      setAdsrPhase(envelope, "release", now);
    }
  } else if (envelope.phase === "release") {
    const progress =
      envelope.releaseMs <= 0 ? 1 : Math.min(elapsed / envelope.releaseMs, 1);
    envelope.currentLevel = envelope.phaseStartLevel * (1 - progress);
    if (progress >= 1) {
      stopAdsrEnvelope(envelope);
      return { active: false, stopped: true };
    }
  }

  return { active: envelope.active, stopped: false };
}

export function getAdsrOutputValue(envelope: AdsrEnvelope) {
  return (
    envelope.originValue +
    (envelope.targetValue - envelope.originValue) *
      envelope.velocityScale *
      envelope.currentLevel
  );
}
