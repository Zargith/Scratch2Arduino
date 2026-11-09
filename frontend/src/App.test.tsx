import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot } from "react-dom/client";
import { describe, expect, it } from "vitest";

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
	host.s2a_startup_project = async () =>
		startupProject ? { path: "/projects/old.s2a", name: "/projects/old.s2a", project: startupProject } : null;
}

describe("application start-up", () => {
	// Regression: a project with variables and functions, given at start-up (double-clicked
	// file), made Blockly fail while building the palette and left a blank window.
	it("opens a project with variables and functions without stopping", async () => {
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
});
