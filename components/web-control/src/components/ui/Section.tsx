import type { ReactNode } from "react";

export function Section({
  actions,
  children,
  label,
}: {
  actions?: ReactNode;
  children: ReactNode;
  label?: string;
}) {
  return (
    <div className="section">
      {label || actions ? (
        <div className="section-heading">
          {label ? <div className="section-label">{label}</div> : <div />}
          {actions ? <div className="section-actions">{actions}</div> : null}
        </div>
      ) : null}
      {children}
    </div>
  );
}
