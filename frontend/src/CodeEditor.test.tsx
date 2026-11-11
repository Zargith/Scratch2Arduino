import { act, createRef } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it, vi } from "vitest";

import { CodeEditor, type CodeEditorHandle } from "@/CodeEditor";
import { starterCode } from "@/program";

describe("code editor", () => {
	it("shows the code, reports edits, and marks problems only when asked", async () => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		// jsdom has no layout: CodeMirror measures text with these.
		const noRects = () => ({ length: 0, item: () => null, [Symbol.iterator]: [][Symbol.iterator] }) as unknown as DOMRectList;
		Range.prototype.getClientRects ??= noRects;
		Range.prototype.getBoundingClientRect ??= () => new DOMRect();
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		const ref = createRef<CodeEditorHandle>();
		const onChange = vi.fn();
		await act(async () => {
			root.render(
				<CodeEditor ref={ref} program="python" language="fr" initialCode={starterCode("python", "fr")} onChange={onChange} />
			);
		});
		const content = container.querySelector(".cm-content");
		expect(content?.textContent).toContain("while True:");
		expect(container.querySelector(".cm-lintRange-error")).toBeNull(); // nothing is checked while typing

		await act(async () => {
			ref.current?.showProblems([{ line: 3, message: "\"led\" est inconnu" }]);
		});
		expect(container.querySelector(".cm-lintRange-error")?.textContent).toContain("pinMode");

		await act(async () => {
			ref.current?.clearProblems();
		});
		expect(container.querySelector(".cm-lintRange-error")).toBeNull();
		expect(onChange).not.toHaveBeenCalled();

		await act(async () => root.unmount());
		container.remove();
	}, 30_000); // CodeMirror's first start is slow in jsdom when the whole suite runs
	// Regression: after a failed Check, a Check that compiled left the underline and the red dot.
	it("clears the marks when a later Check finds nothing", async () => {
		const container = document.createElement("div");
		document.body.append(container);
		const root = createRoot(container);
		const render = (problems: { line: number; message: string }[]) =>
			act(async () => {
				root.render(
					<CodeEditor program="python" language="fr" initialCode={starterCode("python", "fr")} onChange={() => undefined} problems={problems} />
				);
			});
		await render([{ line: 3, message: "erreur" }]);
		expect(container.querySelector(".cm-lintRange-error")).not.toBeNull();
		await render([]);
		expect(container.querySelector(".cm-lintRange-error")).toBeNull();
		expect(container.querySelector(".cm-lint-marker-error")).toBeNull();
		await act(async () => root.unmount());
		container.remove();
	}, 30_000);
});
