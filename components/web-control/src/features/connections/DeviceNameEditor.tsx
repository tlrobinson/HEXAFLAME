import { useEffect, useRef, useState, type KeyboardEvent } from "react";

export function DeviceNameEditor({
  name,
  onRename,
}: {
  name: string;
  onRename: (name: string) => void;
}) {
  const [editing, setEditing] = useState(false);
  const [draftName, setDraftName] = useState(name);
  const inputRef = useRef<HTMLInputElement | null>(null);

  useEffect(() => {
    if (!editing) {
      setDraftName(name);
    }
  }, [editing, name]);

  useEffect(() => {
    if (editing) {
      inputRef.current?.focus();
      inputRef.current?.select();
    }
  }, [editing]);

  function commitRename() {
    const nextName = draftName.trim();
    setEditing(false);
    if (nextName && nextName !== name) {
      onRename(nextName);
    } else {
      setDraftName(name);
    }
  }

  function cancelRename() {
    setDraftName(name);
    setEditing(false);
  }

  function handleKeyDown(event: KeyboardEvent<HTMLInputElement>) {
    if (event.key === "Enter") {
      event.preventDefault();
      commitRename();
    }
    if (event.key === "Escape") {
      event.preventDefault();
      cancelRename();
    }
  }

  if (editing) {
    return (
      <input
        ref={inputRef}
        aria-label="Device name"
        className="connection-name-input"
        type="text"
        value={draftName}
        onBlur={commitRename}
        onChange={(event) => setDraftName(event.currentTarget.value)}
        onKeyDown={handleKeyDown}
      />
    );
  }

  return (
    <button
      className="connection-name-button"
      title="Rename device"
      type="button"
      onClick={() => setEditing(true)}
    >
      {name}
    </button>
  );
}
