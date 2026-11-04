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

export interface OpenedProject {
	path: string;
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
};
