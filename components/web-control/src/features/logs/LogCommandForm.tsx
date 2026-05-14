import { useState, type FormEvent } from "react";
import { sendDeviceCommand } from "./log-store";

export function LogCommandForm({
  buttonId,
  disabled,
  formId,
  inputId,
  placeholder,
  role,
}: {
  buttonId: string;
  disabled: boolean;
  formId: string;
  inputId: string;
  placeholder: string;
  role: "relay" | "stepper";
}) {
  const [value, setValue] = useState("");

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const sent = await sendDeviceCommand(role, value);
    if (sent) {
      setValue("");
    }
  }

  return (
    <form className="command-row device-log-command" id={formId} onSubmit={handleSubmit}>
      <input
        autoComplete="off"
        className="command-input"
        disabled={disabled}
        id={inputId}
        onChange={(event) => setValue(event.target.value)}
        placeholder={placeholder}
        type="text"
        value={value}
      />
      <button className="btn-compact" disabled={disabled} id={buttonId} type="submit">Send</button>
    </form>
  );
}
