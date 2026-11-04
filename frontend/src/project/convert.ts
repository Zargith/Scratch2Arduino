// Conversion between the Blockly workspace state and the project.json format of the C++
// core (core/include/s2a/project.hpp). Both variants open the same .s2a files, so the
// project format is the reference; Blockly state is rebuilt from it.
import { findBlock, shadowInputs, type CatalogBlock } from "@/catalog";

export const FORMAT_NAME = "scratch2arduino-project";
export const FORMAT_VERSION = 1;

export interface ProjectInput {
	value?: string;
	block?: ProjectBlock;
}

export interface ProjectBlock {
	id: string;
	type: string;
	fields?: Record<string, string>;
	inputs?: Record<string, ProjectInput>;
	statements?: Record<string, ProjectBlock[]>;
}

export interface ProjectScript {
	x: number;
	y: number;
	blocks: ProjectBlock[];
}

export interface ProjectVariable {
	id: string;
	name: string;
	initialValue: number;
}

export interface Project {
	format: typeof FORMAT_NAME;
	formatVersion: number;
	name: string;
	board: string;
	variables: ProjectVariable[];
	scripts: ProjectScript[];
}

// --- Blockly serialization state (subset used here) ---------------------------------

type FieldState = string | number | { id: string };

export interface BlockState {
	type: string;
	id?: string;
	x?: number;
	y?: number;
	fields?: Record<string, FieldState>;
	inputs?: Record<string, { shadow?: BlockState; block?: BlockState }>;
	next?: { block?: BlockState };
}

export interface WorkspaceState {
	blocks?: { languageVersion: number; blocks: BlockState[] };
	variables?: { id: string; name: string; type?: string }[];
}

export function emptyProject(name: string, board: string): Project {
	return { format: FORMAT_NAME, formatVersion: FORMAT_VERSION, name, board, variables: [], scripts: [] };
}

// --- Blockly -> project --------------------------------------------------------------

function chain(first: BlockState | undefined): BlockState[] {
	const result: BlockState[] = [];
	for (let block = first; block; block = block.next?.block) result.push(block);
	return result;
}

function shadowValue(shadow: BlockState | undefined): string | undefined {
	const field = shadow?.fields?.NUM ?? shadow?.fields?.TEXT;
	return field === undefined || typeof field === "object" ? undefined : String(field);
}

function toProjectBlock(state: BlockState): ProjectBlock {
	const definition = findBlock(state.type);
	const result: ProjectBlock = { id: state.id ?? "", type: state.type };
	const fields: Record<string, string> = {};
	const inputs: Record<string, ProjectInput> = {};
	const statements: Record<string, ProjectBlock[]> = {};

	for (const [name, value] of Object.entries(state.fields ?? {})) {
		fields[name] = typeof value === "object" ? value.id : String(value);
	}
	for (const [name, input] of Object.entries(state.inputs ?? {})) {
		const kind = definition?.args?.find((arg) => arg.name === name)?.kind;
		if (kind === "statement") {
			statements[name] = chain(input.block).map(toProjectBlock);
			continue;
		}
		const converted: ProjectInput = {};
		const value = shadowValue(input.shadow);
		if (value !== undefined) converted.value = value;
		if (input.block) converted.block = toProjectBlock(input.block);
		inputs[name] = converted;
	}
	if (Object.keys(fields).length > 0) result.fields = fields;
	if (Object.keys(inputs).length > 0) result.inputs = inputs;
	if (Object.keys(statements).length > 0) result.statements = statements;
	return result;
}

export function workspaceToProject(
	state: WorkspaceState,
	meta: { name: string; board: string; initialValues?: Record<string, number> }
): Project {
	const project = emptyProject(meta.name, meta.board);
	project.variables = (state.variables ?? [])
		.filter((variable) => (variable.type ?? "") === "")
		.map((variable) => ({
			id: variable.id,
			name: variable.name,
			initialValue: meta.initialValues?.[variable.id] ?? 0,
		}));
	project.scripts = (state.blocks?.blocks ?? []).map((top) => ({
		x: Math.round(top.x ?? 0),
		y: Math.round(top.y ?? 0),
		blocks: chain(top).map(toProjectBlock),
	}));
	return project;
}

// --- project -> Blockly --------------------------------------------------------------

function toBlockState(block: ProjectBlock): BlockState {
	const definition: CatalogBlock | undefined = findBlock(block.type);
	const state: BlockState = { type: block.type, id: block.id };
	const fields: Record<string, FieldState> = {};
	const inputs: NonNullable<BlockState["inputs"]> = {};

	for (const [name, value] of Object.entries(block.fields ?? {})) {
		const kind = definition?.args?.find((arg) => arg.name === name)?.kind;
		fields[name] = kind === "variable" ? { id: value } : value;
	}
	if (definition) {
		for (const [name, { shadow }] of Object.entries(shadowInputs(definition))) {
			inputs[name] = { shadow: structuredClone(shadow) as unknown as BlockState };
		}
	}
	for (const [name, input] of Object.entries(block.inputs ?? {})) {
		const target = (inputs[name] ??= {});
		const shadowFields = target.shadow?.fields;
		if (input.value !== undefined && shadowFields) {
			shadowFields[shadowFields.TEXT !== undefined ? "TEXT" : "NUM"] = input.value;
		}
		if (input.block) target.block = toBlockState(input.block);
	}
	for (const [name, stack] of Object.entries(block.statements ?? {})) {
		const first = linked(stack);
		if (first) inputs[name] = { block: first };
	}
	if (Object.keys(fields).length > 0) state.fields = fields;
	if (Object.keys(inputs).length > 0) state.inputs = inputs;
	return state;
}

function linked(stack: ProjectBlock[]): BlockState | undefined {
	let next: BlockState | undefined;
	for (const block of [...stack].reverse()) {
		const state = toBlockState(block);
		if (next) state.next = { block: next };
		next = state;
	}
	return next;
}

/**
 * Blocks below a terminal block ("forever") cannot be attached in Blockly: they are moved to a
 * script of their own next to it (they never ran anyway, and the validator warned about them).
 */
function splitAfterTerminal(stack: ProjectBlock[], overflow: ProjectBlock[][]): ProjectBlock[] {
	const end = stack.findIndex((block) => findBlock(block.type)?.terminal === true);
	const kept = end >= 0 ? stack.slice(0, end + 1) : stack;
	if (end >= 0 && end + 1 < stack.length) overflow.push(stack.slice(end + 1));
	return kept.map((block) =>
		block.statements
			? {
					...block,
					statements: Object.fromEntries(
						Object.entries(block.statements).map(([name, inner]) => [name, splitAfterTerminal(inner, overflow)])
					),
				}
			: block
	);
}

export function projectToWorkspace(project: Project): WorkspaceState {
	const blocks: BlockState[] = [];
	for (const script of project.scripts) {
		const overflow: ProjectBlock[][] = [];
		const first = linked(splitAfterTerminal(script.blocks, overflow));
		if (first) blocks.push({ ...first, x: script.x, y: script.y });
		overflow.forEach((stack, index) => {
			const loose = linked(stack);
			if (loose) blocks.push({ ...loose, x: script.x + 320, y: script.y + 60 * index });
		});
	}
	return {
		blocks: { languageVersion: 0, blocks },
		variables: project.variables.map((variable) => ({ id: variable.id, name: variable.name, type: "" })),
	};
}

/** Loose check before handing a file to the editor; the C++ core validates fully. */
export function isProject(value: unknown): value is Project {
	if (typeof value !== "object" || value === null) return false;
	const candidate = value as Partial<Project>;
	return (
		candidate.format === FORMAT_NAME &&
		typeof candidate.board === "string" &&
		Array.isArray(candidate.scripts) &&
		Array.isArray(candidate.variables)
	);
}
