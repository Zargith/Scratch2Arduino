// Mounts the whole editor against a fake host (apps/webview), for the App flow tests.
import { MantineProvider } from "@mantine/core";
import { act } from "react";
import { createRoot, type Root } from "react-dom/client";

import { App } from "@/App";

type HostFunction = (...args: never[]) => Promise<unknown>;

/** What the editor calls at start-up, answered as a host with nothing saved would. */
export function defaultHost(): Record<string, HostFunction> {
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
export function installHost(functions: Record<string, HostFunction>): void {
	clearHost();
	Object.assign(window as unknown as Record<string, unknown>, functions);
}

/** A project given at start-up (double-clicked file), as the host returns it. */
export function startupProject(project: unknown): HostFunction {
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

export interface MountedApp {
	container: HTMLDivElement;
	errors: unknown[];
	/** Clicks the button or label whose text is `text` (or starts with "`text` ("). */
	click(text: string): Promise<void>;
	text(): string;
	unmount(): Promise<void>;
}

export async function mountApp(): Promise<MountedApp> {
	const container = document.createElement("div");
	document.body.appendChild(container);
	const errors: unknown[] = [];
	let root: Root | null = null;
	await act(async () => {
		root = createRoot(container, { onUncaughtError: (error) => errors.push(error) });
		root.render(
			<MantineProvider>
				<App />
			</MantineProvider>
		);
	});
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
		async unmount() {
			await act(async () => root?.unmount());
			container.remove();
			clearHost();
		},
	};
}
