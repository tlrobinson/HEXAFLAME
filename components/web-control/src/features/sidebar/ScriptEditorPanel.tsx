import type { KeyboardEvent } from "react";
import { Section } from "../../components/ui/Section";
import { useScriptEditor } from "./script-editor-store";

export function ScriptEditorPanel() {
  const { callbacks, snapshot } = useScriptEditor();

  function handleKeyDown(event: KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key !== "Tab") {
      return;
    }
    event.preventDefault();
    const target = event.currentTarget;
    const start = target.selectionStart;
    const end = target.selectionEnd;
    const nextScript =
      snapshot.script.substring(0, start) +
      "  " +
      snapshot.script.substring(end);
    callbacks.onChange(nextScript);
    window.requestAnimationFrame(() => {
      target.selectionStart = target.selectionEnd = start + 2;
    });
  }

  return (
    <Section>
      <div className="section-row-wrap">
        <button type="button" onClick={callbacks.onToggleOpen}>
          {snapshot.open ? "Hide Script" : "Edit Script"}
        </button>
      </div>
      <div style={{ display: snapshot.open ? "" : "none" }}>
        <textarea
          className="animation-editor"
          rows={8}
          spellCheck={false}
          value={snapshot.script}
          onChange={(event) => callbacks.onChange(event.currentTarget.value)}
          onKeyDown={handleKeyDown}
        />
        <div className="editor-actions">
          <button type="button" onClick={callbacks.onRestore}>Restore</button>
          <button type="button" onClick={callbacks.onToggleHelp}>?</button>
        </div>
        <div className="script-help" style={{ display: snapshot.helpOpen ? "" : "none" }}>
          <h3>Script Reference</h3>
          <dl>
            <dt>Selectors</dt>
            <dd><code>all</code> &mdash; every node</dd>
            <dd><code>center</code> &mdash; origin node</dd>
            <dd><code>type:center</code> / <code>type:vertex</code></dd>
            <dd><code>dist:N</code> &mdash; nodes at distance N</dd>
            <dd><code>dist:N..M</code> &mdash; distance range</dd>
            <dt>Variables</dt>
            <dd><code>$d</code> &mdash; dist loop index</dd>
            <dd><code>$max</code> &mdash; max distance</dd>
            <dd><code>$n</code> &mdash; current node in for-node</dd>
            <dd>Arithmetic: <code>$d+1</code> <code>$d-1</code> <code>$max-1</code></dd>
            <dt>Frame Commands</dt>
            <dd><code>clear</code> / <code>add</code> / <code>remove</code> / <code>set</code> / <code>frame</code></dd>
            <dt>Loops</dt>
            <dd><code>for-dist [from] [to]</code> &mdash; ascending</dd>
            <dd><code>for-dist-rev [from] [to]</code> &mdash; descending</dd>
            <dd><code>for-node &lt;sel&gt; [sort angle|shell-angle]</code></dd>
            <dd><code>end</code></dd>
            <dt>Walk</dt>
            <dd><code>cursor &lt;sel&gt;</code></dd>
            <dd><code>walk-to $n [constrain &lt;sel&gt;]</code></dd>
            <dd><code>dfs [sort shell-angle]</code></dd>
            <dt>Filter</dt>
            <dd><code>filter-scene type:vertex</code></dd>
          </dl>
          <p><code># comments</code> are ignored</p>
        </div>
        {snapshot.error ? (
          <div className="editor-error">{snapshot.error}</div>
        ) : null}
      </div>
    </Section>
  );
}
