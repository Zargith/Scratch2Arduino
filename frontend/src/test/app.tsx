// Mounts the whole editor against a fake host (apps/webview), for the App flow tests.
import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot, type Root } from "react-dom/client";
import * as Blockly from "scratch-blocks";

import { App } from "@/App";
import type { HostFunctions, OpenedProject } from "@/bridge";

/** Host functions a test answers: typed against the real contract (bridge.ts), so drift fails tsc. */
export type FakeHost = Partial<HostFunctions>;

/** What the editor calls at start-up, answered as a host with nothing saved would. */
export function defaultHost(): FakeHost {
	return {
		s2a_hello: async () => ({ language: "fr", version: "test" }),
		s2a_settings: async () => ({ language: "fr", checkUpdates: false, theme: "light" }),
		s2a_ports: async () => [],
		s2a_set_title: async () => undefined,
		s2a_autosaved: async () => null,
		s2a_autosave: async () => null,
		s2a_autosave_clear: async () => null,
		s2a_library: async () => [],
		s2a_recent: async () => [],
		s2a_generate: async () => "void setup() {}\nvoid loop() {}\n",
		s2a_code_declarations: async () => [],
		s2a_startup_project: async () => null,
	};
}

/** Removes the host functions: a late call from an unmounted editor then fails instead of answering. */
export function clearHost(): void {
	const host = window as unknown as Record<string, unknown>;
	for (const name of Object.keys(host)) if (name.startsWith("s2a_")) delete host[name];
}

/** Installs the host functions on window (and removes those of a previous test). */
export function installHost(functions: FakeHost): void {
	clearHost();
	Object.assign(window as unknown as Record<string, unknown>, functions);
}

/** A project given at start-up (double-clicked file), as the host returns it. */
export function startupProject(project: OpenedProject["project"]): NonNullable<FakeHost["s2a_startup_project"]> {
	return async () => ({ path: "/projets/test.s2a", name: "/projets/test.s2a", project });
}

/** Waits until `ready()` holds (or fails after `timeout` ms), letting React and timers run. */
export async function waitUntil(ready: () => boolean, what = "the condition", timeout = 20_000): Promise<void> {
	const start = Date.now();
	while (!ready()) {
		if (Date.now() - start > timeout) throw new Error(`waitUntil: still waiting for ${what}`);
		await act(async () => new Promise((done) => setTimeout(done, 20)));
	}
}

// jsdom works out the whole CSS cascade (Mantine + Blockly sheets) on every getComputedStyle call,
// and Blockly calls it for each block it measures: mounting the editor took ten minutes and more
// (audit #201). jsdom lays nothing out anyway, so while the editor is mounted getComputedStyle
// returns the inline style only: a test of the whole editor cannot check a value that comes from a
// stylesheet (display, visibility...). The other tests keep the real one.
const realComputedStyle = window.getComputedStyle;
const emptyStyle = document.createElement("div").style;
const inlineStyleOnly = ((element: Element) => (element as HTMLElement).style ?? emptyStyle) as typeof window.getComputedStyle;

export interface MountedApp {
	container: HTMLDivElement;
	errors: unknown[];
	/** Clicks the button or label whose text is `text` (or starts with "`text` ("). */
	click(text: string): Promise<void>;
	text(): string;
	/**
	 * The block editor of this mount, or null. Blockly.getMainWorkspace() is global: it can still
	 * point at the editor of an earlier test, whose block ids may be the same ("start").
	 */
	workspace(): Blockly.WorkspaceSvg | null;
	unmount(): Promise<void>;
}

export async function mountApp(): Promise<MountedApp> {
	const container = document.createElement("div");
	document.body.appendChild(container);
	const errors: unknown[] = [];
	let root: Root | null = null;
	// One editor at a time: a second mount would get the real getComputedStyle back on the first unmount.
	if (window.getComputedStyle === inlineStyleOnly) throw new Error("mountApp: an editor is already mounted (unmount it first)");
	window.getComputedStyle = inlineStyleOnly;
	try {
		await act(async () => {
			root = createRoot(container, { onUncaughtError: (error) => errors.push(error) });
			root.render(
				<MantineProvider>
					<App />
				</MantineProvider>
			);
		});
	} catch (error) {
		// Nothing would unmount it: leave no editor, page node or host behind for the next test.
		try {
			await act(async () => (root as Root | null)?.unmount());
		} finally {
			container.remove();
			clearHost();
			window.getComputedStyle = realComputedStyle;
		}
		throw error;
	}
	const find = (text: string) =>
		[...document.querySelectorAll<HTMLElement>("label, button")].find((node) => {
			const label = node.textContent?.trim() ?? "";
			return label === text || label.startsWith(`${text} (`);
		});
	return {
		container,
		errors,
		async click(text) {
			await waitUntil(() => find(text) !== undefined, `"${text}"`);
			await act(async () => find(text)!.click());
		},
		text: () => document.body.textContent ?? "",
		workspace() {
			const main = Blockly.getMainWorkspace() as Blockly.WorkspaceSvg | null;
			return main && container.contains(main.getInjectionDiv()) ? main : null;
		},
		async unmount() {
			try {
				await act(async () => root?.unmount());
			} finally {
				container.remove();
				clearHost();
				window.getComputedStyle = realComputedStyle;
			}
		},
	};
}
