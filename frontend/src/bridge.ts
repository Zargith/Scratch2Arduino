// Calls into the native C++ host (apps/webview/src/bridge.cpp). The host binds each
// function with webview::bind; every call returns a promise. When the page runs in a
// plain browser (pnpm dev) the host is missing and calls report it instead of throwing.
import type { Language } from "@/catalog";
import type { SavedGroup } from "@/myBlocks";
import type { UserDeclaration } from "@/blocks/definitions";
import type { CodeProblem } from "@/program";
import type { Project } from "@/project/convert";

export interface HostInfo {
	language: Language;
	version: string;
}

/** Wi-Fi of an ESP32 after s2a_wifi_setup (see core LiveLink::NetworkState). */
export interface WifiSetup {
	ok: boolean;
	message: string;
	accessPoint: string;
	accessPointAddress: string;
	/** On the home network, "" when not joined. */
	address: string;
}

/** Wiring of a project (s2a_wiring), already in the user's language. */
export interface WiringPlan {
	components: {
		kind: string;
		name: string;
		connections: { componentPin: string; boardPin: string; via: string }[];
		note: string;
		blockIds: string[];
	}[];
	warnings: { message: string; blockIds: string[] }[];
	/** The diagram drawn by the core (SVG), absent for an unknown board. */
	svg?: string;
}

export interface Port {
	address: string;
	label: string;
	boardId?: string;
	/** The serial port of a board paired over Bluetooth (macOS): for the Live mode only. */
	bluetooth?: boolean;
}

/** How an HC-05 / HC-06 module is wired to an Arduino board (see core live::BluetoothModule). */
export interface BluetoothModule {
	/** Board pin that receives (label printed on the board): wired to TXD of the module. */
	rx: string;
	/** Board pin that sends (label printed on the board): wired to RXD of the module. */
	tx: string;
	baud: number;
}

/** Result of s2a_bluetooth_module. */
export interface ModuleSetup {
	ok: boolean;
	message: string;
}

/** Result of s2a_bluetooth_search. */
export interface BluetoothSearch {
	ok: boolean;
	/** False on macOS: the board is paired in the system settings, then it is a serial port. */
	byAddress?: boolean;
	devices?: { name: string; address: string }[];
	message?: string;
	details?: string;
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
	/** Text programs: the faulty lines (C++ compiler errors, Python/JavaScript syntax). */
	problems?: CodeProblem[];
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
	/** Last lines written by the program ("write to the serial monitor", print, printError)... */
	output?: OutputLine[];
	/** ...out of this many since Live mode started. */
	outputCount?: number;
	/** Python/JavaScript: line of the error that stopped the program (0: none)... */
	line?: number;
	/** ...in the program ("") or in "My code" ("user"). */
	file?: string;
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

export interface RecentProject {
	path: string;
	/** File name without its extension. */
	name: string;
}

export interface AutosavedProject {
	project: Project;
	/** Where Save writes (the .s2a file it came from), or empty. */
	path: string;
	/** Seconds since 1970-01-01 UTC. */
	savedAt: number;
}

/** A line of the Live output: printed, or on the error output (printError, console.error, the error that stopped it). */
export interface OutputLine {
	text: string;
	error: boolean;
}

/** Settings of the editor, kept by the host (settings.json in the user's folder). */
export interface Settings {
	/** "fr" or "en"; "": the language of the system. */
	language: Language | "";
	/** Look for a newer release in the background at start-up. */
	checkUpdates: boolean;
	/** "light" or "dark"; "": the one of the system. */
	theme: "light" | "dark" | "";
}

/** A program rewritten in another language (core convert::convert). */
export interface Converted {
	code: string;
	/** Lines of the original code kept as comments, to convert by hand. */
	toCheck: number[];
}

interface HostFunctions {
	s2a_hello(): Promise<HostInfo>;
	s2a_generate(project: Project): Promise<string>;
	s2a_code_declarations(language: string, code: string): Promise<UserDeclaration[]>;
	s2a_wiring(project: Project, language: Language): Promise<WiringPlan>;
	s2a_save_image(name: string, png: string, language: Language): Promise<string | null>;
	s2a_open(language: Language): Promise<OpenedProject | null>;
	s2a_startup_project?(language: Language): Promise<OpenedProject | null>;
	s2a_save(project: Project, path: string, language: Language): Promise<string | null>;
	s2a_recent(): Promise<RecentProject[]>;
	s2a_settings(): Promise<Settings>;
	s2a_save_settings(settings: Settings): Promise<boolean>;
	s2a_open_url(url: string): Promise<boolean>;
	s2a_convert(code: string, from: string, to: string): Promise<Converted>;
	s2a_open_recent(path: string, language: Language): Promise<OpenedProject>;
	s2a_autosave(project: Project, path: string): Promise<null>;
	s2a_autosaved(): Promise<AutosavedProject | null>;
	s2a_autosave_clear(): Promise<null>;
	s2a_library(): Promise<SavedGroup[]>;
	s2a_library_add(name: string, blocks: Project): Promise<SavedGroup>;
	s2a_library_rename(id: string, name: string): Promise<null>;
	s2a_library_remove(id: string): Promise<null>;
	s2a_library_export(language: Language): Promise<string | null>;
	s2a_library_import(language: Language): Promise<number | null>;
	s2a_ports(): Promise<Port[]>;
	s2a_wifi_setup(port: string, name: string, password: string, boardId: string, language: Language): Promise<WifiSetup>;
	s2a_bluetooth_search(language: Language): Promise<BluetoothSearch>;
	s2a_bluetooth_module(
		port: string,
		boardId: string,
		module: BluetoothModule | null,
		language: Language,
	): Promise<ModuleSetup>;
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
	settings: () => host("s2a_settings")(),
	saveSettings: (settings: Settings) => host("s2a_save_settings")(settings),
	/** Opens a page of the project's GitHub repository in the browser (the host refuses any other). */
	openUrl: (url: string) => host("s2a_open_url")(url),
	/** Rewrites C++, Python or JavaScript code in another of these languages (never fails). */
	convert: (code: string, from: string, to: string) => host("s2a_convert")(code, from, to),
	generate: (project: Project) => host("s2a_generate")(project),
	/** Functions and variables of a "My code" file, found by the core (never fails). */
	codeDeclarations: (language: string, code: string) => host("s2a_code_declarations")(language, code),
	/** Components to wire, computed by the core from the blocks (core/include/s2a/wiring.hpp). */
	wiring: (project: Project, language: Language) => host("s2a_wiring")(project, language),
	/** Saves a PNG (base64 or data URL) where the user chooses; resolves to the file, or null when cancelled. */
	saveImage: (name: string, png: string, language: Language) => host("s2a_save_image")(name, png, language),
	open: (language: Language) => host("s2a_open")(language),
	/** Project given to the program (dropped on its icon); null when none or an older host. */
	startupProject: async (language: Language): Promise<OpenedProject | null> =>
		typeof window.s2a_startup_project === "function" ? window.s2a_startup_project(language) : null,
	/** Empty path: ask where to save. Resolves to the saved path, or null when cancelled. */
	save: (project: Project, path: string, language: Language) => host("s2a_save")(project, path, language),
	recent: () => host("s2a_recent")(),
	openRecent: (path: string, language: Language) => host("s2a_open_recent")(path, language),
	/** Keeps a copy of the project until it is saved (path: where it was saved, or empty). */
	autosave: (project: Project, path: string) => host("s2a_autosave")(project, path),
	autosaved: () => host("s2a_autosaved")(),
	autosaveClear: () => host("s2a_autosave_clear")(),
	library: () => host("s2a_library")(),
	libraryAdd: (name: string, blocks: Project) => host("s2a_library_add")(name, blocks),
	libraryRename: (id: string, name: string) => host("s2a_library_rename")(id, name),
	libraryRemove: (id: string) => host("s2a_library_remove")(id),
	/** Resolves to the written file, or null when cancelled. */
	libraryExport: (language: Language) => host("s2a_library_export")(language),
	/** Resolves to the number of groups added, or null when cancelled. */
	libraryImport: (language: Language) => host("s2a_library_import")(language),
	ports: () => host("s2a_ports")(),
	/** Saves the home Wi-Fi on the board over its USB port (empty name: forget it). */
	wifiSetup: (port: string, name: string, password: string, boardId: string, language: Language) =>
		host("s2a_wifi_setup")(port, name, password, boardId, language),
	/** Bluetooth boards around the computer (about 8 seconds). */
	bluetoothSearch: (language: Language) => host("s2a_bluetooth_search")(language),
	/** Saves the pins of an HC-05 / HC-06 module on the board over its USB port (null: forget it). */
	bluetoothModule: (port: string, boardId: string, module: BluetoothModule | null, language: Language) =>
		host("s2a_bluetooth_module")(port, boardId, module, language),
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
