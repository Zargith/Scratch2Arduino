import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { EditorView } from "@codemirror/view";
import { beforeAll, describe, expect, it } from "vitest";

import { App } from "@/App";

/** The host (apps/webview), replaced by what the editor needs at start-up. */
function fakeHost(startupProject: unknown) {
	const host = window as unknown as Record<string, unknown>;
	host.s2a_hello = async () => ({ language: "fr", version: "test" });
	host.s2a_ports = async () => [];
	host.s2a_set_title = async () => undefined;
	host.s2a_autosaved = async () => null;
	host.s2a_autosave_clear = async () => null;
	host.s2a_library = async () => [];
	host.s2a_recent = async () => [];
	host.s2a_generate = async () => "void setup() {}\nvoid loop() {}\n";
	host.s2a_code_declarations = async () => [];
	host.s2a_startup_project = async () =>
		startupProject ? { path: "/projects/old.s2a", name: "/projects/old.s2a", project: startupProject } : null;
}

describe("application start-up", () => {
	beforeAll(() => {
		(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;
		window.matchMedia ??= (() => ({
			matches: false,
			addEventListener() {},
			removeEventListener() {},
			addListener() {},
			removeListener() {},
		})) as never;
		globalThis.ResizeObserver ??= class {
			observe() {}
			unobserve() {}
			disconnect() {}
		} as never;
	});

	// Regression: a project with variables and functions, given at start-up (double-clicked
	// file), made Blockly fail while building the palette and left a blank window.
	it("opens a project with variables and functions without stopping", async () => {
		const project = JSON.parse(readFileSync(resolve(__dirname, "../../examples/projects/all-blocks/project.json"), "utf8"));
		expect(project.functions?.length).toBeGreaterThan(0);
		fakeHost(project);

		const errors: unknown[] = [];
		const container = document.createElement("div");
		document.body.appendChild(container);
		const root = createRoot(container, { onUncaughtError: (error) => errors.push(error) });
		await act(async () => {
			root.render(
				<MantineProvider>
					<App />
				</MantineProvider>
			);
		});
		await act(async () => {
			await new Promise((done) => setTimeout(done, 300));
		});
		expect(errors).toEqual([]);
		expect(container.innerHTML).not.toBe("");
		await act(async () => root.unmount());
		container.remove();
	}, 120_000);
	// Regression: what was typed in "My code" came back as the starter code after going to the
	// blocks (or to the other tab) and back, until the project was saved and opened again.
	it("keeps what was typed in My code when switching tabs and views", async () => {
		const project = {
			format: "scratch2arduino-project",
			formatVersion: 2,
			name: "Mon code",
			board: "uno",
			variables: [],
			scripts: [{ x: 40, y: 40, blocks: [{ id: "start", type: "events_when_program_starts" }] }],
			userCode: { language: "python", code: "vitesse = 1\n" },
		};
		fakeHost(project);
		// CodeMirror measures text ranges, which jsdom does not lay out.
		Range.prototype.getClientRects ??= () => [] as unknown as DOMRectList;
		Range.prototype.getBoundingClientRect ??= () => new DOMRect();
		const container = document.createElement("div");
		document.body.appendChild(container);
		const root = createRoot(container);
		const settle = () => act(async () => new Promise((done) => setTimeout(done, 100)));
		const click = async (text: string) => {
			const target = [...container.querySelectorAll("label, button")].find((node) => {
				const label = node.textContent?.trim() ?? "";
				return label === text || label.startsWith(`${text} (`);
			});
			expect(target, text).toBeDefined();
			await act(async () => (target as HTMLElement).click());
			await settle();
		};
		const mine = () => {
			const content = container.querySelector(".cm-content");
			expect(content).not.toBeNull();
			return EditorView.findFromDOM(content as HTMLElement);
		};
		await act(async () => {
			root.render(
				<MantineProvider>
					<App />
				</MantineProvider>
			);
		});
		await settle();
		await click("Code");
		await click("Mon code");
		const view = mine();
		expect(view?.state.doc.toString()).toBe("vitesse = 1\n");
		await act(async () => view?.dispatch({ changes: { from: 0, insert: "# à moi\n" } }));

		await click("Code des blocs");
		await click("Mon code");
		expect(mine()?.state.doc.toString()).toBe("# à moi\nvitesse = 1\n");

		await click("Blocs");
		await click("Code");
		await click("Mon code");
		expect(mine()?.state.doc.toString()).toBe("# à moi\nvitesse = 1\n");
		await act(async () => root.unmount());
		container.remove();
	}, 120_000);
});
