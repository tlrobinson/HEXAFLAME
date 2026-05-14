import type { ReactNode } from "react";

export function Section({
  children,
  label,
}: {
  children: ReactNode;
  label?: string;
}) {
  return (
    <div className="section">
      {label ? <div className="section-label">{label}</div> : null}
      {children}
    </div>
  );
}
