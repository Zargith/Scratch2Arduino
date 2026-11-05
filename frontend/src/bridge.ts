// Calls into the native C++ host (apps/webview/src/bridge.cpp). The host binds each
// function with webview::bind; every call returns a promise. When the page runs in a
// plain browser (pnpm dev) the host is missing and calls report it instead of throwing.
import type { Language } from "@/catalog";
import type { Project } from "@/project/convert";

export interface HostInfo {
	language: Language;
	version: string;
}

export interface Port {
	address: string;
	label: string;
	boardId?: string;
}

/** Result of a build or an upload, already explained in the user's language by the core. */
export interface Outcome {
	ok: boolean;
	message: string;
	details?: string;
	/** Blocks to show: validation errors, or where the compiler errors point. */
	blockIds?: string[];
	/** Validation warnings: the program was built anyway. */
	warnings?: string[];
	warningBlockIds?: string[];
}

export type LiveState = "connecting" | "flashing" | "running" | "finished" | "stopped" | "failed";

export interface LiveSnapshot {
	state: LiveState;
	message?: string;
	details?: string;
	running?: string[];
	problems?: string[];
	variables?: Record<string, number>;
	/** Sensor values read by the blocks, by pin label ("A0", "2"). */
	readings?: Record<string, number>;
	/** Last lines written by "write to the serial monitor"... */
	output?: string[];
	/** ...out of this many since Live mode started. */
	outputCount?: number;
}

export interface MonitorState {
	open: boolean;
	lines?: string[];
	count?: number;
	/** Why the connection stopped (board unplugged...), for the user. */
	message?: string;
	details?: string;
}

export interface OpenedProject {
	/** Where Save writes; empty for a project.json, which is never overwritten. */
	path: string;
	/** The file that was opened. */
	name?: string;
	project: Project;
}

interface HostFunctions {
	s2a_hello(): Promise<HostInfo>;
	s2a_generate(project: Project): Promise<string>;
	s2a_open(language: Language): Promise<OpenedProject | null>;
	s2a_save(project: Project, path: string, language: Language): Promise<string | null>;
	s2a_ports(): Promise<Port[]>;
	s2a_build(project: Project, language: Language): Promise<Outcome>;
	s2a_upload(project: Project, port: string, language: Language): Promise<Outcome>;
	s2a_set_title(title: string): Promise<void>;
	s2a_live_start(project: Project, port: string, language: Language): Promise<{ ok: boolean; message?: string }>;
	s2a_live_update(project: Project): Promise<void>;
	s2a_live_stop(): Promise<void>;
	s2a_live_state(): Promise<LiveSnapshot>;
	s2a_monitor_open(port: string, baud: number, language: Language): Promise<{ ok: boolean; message?: string; details?: string }>;
	s2a_monitor_send(line: string): Promise<void>;
	s2a_monitor_close(): Promise<void>;
	s2a_monitor_state(): Promise<MonitorState>;
}

declare global {
	interface Window extends Partial<HostFunctions> {}
}

export class HostMissingError extends Error {
	constructor() {
		super("native host not available");
	}
}

function host<K extends keyof HostFunctions>(name: K): HostFunctions[K] {
	const fn = window[name];
	if (typeof fn !== "function") throw new HostMissingError();
	return fn as HostFunctions[K];
}

export const hasHost = (): boolean => typeof window.s2a_hello === "function";

export const bridge = {
	hello: () => host("s2a_hello")(),
	generate: (project: Project) => host("s2a_generate")(project),
	open: (language: Language) => host("s2a_open")(language),
	/** Empty path: ask where to save. Resolves to the saved path, or null when cancelled. */
	save: (project: Project, path: string, language: Language) => host("s2a_save")(project, path, language),
	ports: () => host("s2a_ports")(),
	build: (project: Project, language: Language) => host("s2a_build")(project, language),
	upload: (project: Project, port: string, language: Language) => host("s2a_upload")(project, port, language),
	setTitle: (title: string) => host("s2a_set_title")(title),
	liveStart: (project: Project, port: string, language: Language) => host("s2a_live_start")(project, port, language),
	liveUpdate: (project: Project) => host("s2a_live_update")(project),
	liveStop: () => host("s2a_live_stop")(),
	liveState: () => host("s2a_live_state")(),
	monitorOpen: (port: string, baud: number, language: Language) => host("s2a_monitor_open")(port, baud, language),
	monitorSend: (line: string) => host("s2a_monitor_send")(line),
	monitorClose: () => host("s2a_monitor_close")(),
	monitorState: () => host("s2a_monitor_state")(),
};
