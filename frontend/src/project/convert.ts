// Conversion between the Blockly workspace state and the project.json format of the C++
// core (core/include/s2a/project.hpp). The C++ core reads the same .s2a files, so the
// project format is the reference; Blockly state is rebuilt from it.
import { findBlock, shadowInputs, type CatalogBlock } from "@/catalog";

export const FORMAT_NAME = "scratch2arduino-project";
/** Version of block projects (read by every Scratch2Arduino version). */
export const FORMAT_VERSION = 1;
/** Version of projects written as text (see core/include/s2a/project.hpp, kFormatVersion). */
export const TEXT_FORMAT_VERSION = 2;

/** How the program of a project is written (core project::ProgramLanguage). */
export type ProgramLanguage = "blocks" | "cpp" | "python" | "javascript";

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

export interface ProjectFunction {
	id: string;
	name: string;
	/** Names of its number inputs (slots ARG1..ARG4 of "call"); left out when it has none. */
	parameters?: string[];
}

/** Most inputs a function can have (core project::kMaxFunctionParameters). */
export const MAX_FUNCTION_PARAMETERS = 4;

export interface Project {
	format: typeof FORMAT_NAME;
	formatVersion: number;
	name: string;
	board: string;
	variables: ProjectVariable[];
	/** Left out when the project has none (as the C++ core writes it). */
	functions?: ProjectFunction[];
	scripts: ProjectScript[];
	/** Left out for blocks. A text project keeps its blocks beside the code. */
	language?: ProgramLanguage;
	/** The program of a text project. */
	code?: string;
	/** The user's own code file ("My code"), used with the blocks. */
	userCode?: UserCode;
	/** Arduino libraries chosen in the library manager; left out when there are none. */
	libraries?: ProjectLibrary[];
}

/** An Arduino library the project needs (core project::Library). */
export interface ProjectLibrary {
	name: string;
	/** Left out: any version (the latest is installed when missing). */
	version?: string;
}

/** The project with its list of libraries (left out when empty, as the C++ core writes it). */
export function withLibraries(project: Project, libraries: ProjectLibrary[]): Project {
	const { libraries: _libraries, ...rest } = project;
	if (libraries.length === 0) return rest;
	return { ...rest, libraries: libraries.map((library) => (library.version ? { name: library.name, version: library.version } : { name: library.name })) };
}

/** The "My code" file of a block project (core project::UserCode). */
export interface UserCode {
	language: Exclude<ProgramLanguage, "blocks">;
	code: string;
}

/** The project with (or without) a "My code" file: format 2 when it has one. */
export function withUserCode(project: Project, userCode: UserCode | null): Project {
	const { userCode: _userCode, ...rest } = project;
	if (!userCode) return rest.language && rest.language !== "blocks" ? rest : { ...rest, formatVersion: FORMAT_VERSION };
	return { ...rest, formatVersion: TEXT_FORMAT_VERSION, userCode };
}

/** The project with its program written as `language` (a block project when "blocks"). */
export function withProgram(project: Project, language: ProgramLanguage, code: string): Project {
	const { language: _language, code: _code, ...rest } = project;
	if (language === "blocks") return { ...rest, formatVersion: rest.userCode ? TEXT_FORMAT_VERSION : FORMAT_VERSION };
	return { ...rest, formatVersion: TEXT_FORMAT_VERSION, language, code };
}

/** Type of the Blockly variables that stand for functions (see blocks/definitions.ts). */
const FUNCTION_TYPE = "function";

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
	/** "My code" call and result blocks: the function and its number of slots. */
	extraState?: unknown;
}

export interface WorkspaceState {
	blocks?: { languageVersion: number; blocks: BlockState[] };
	variables?: { id: string; name: string; type?: string }[];
	/** Function id -> names of its inputs (only functions that have some). Saved by the editor. */
	functionParameters?: Record<string, string[]>;
}

/** Blocks of a function call and definition: their extra state names the inputs of the function. */
export const FUNCTION_CALL = "functions_call";
export const FUNCTION_DEFINE = "functions_define";

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
	const functions = (state.variables ?? [])
		.filter((variable) => variable.type === FUNCTION_TYPE)
		.map((variable): ProjectFunction => {
			const parameters = state.functionParameters?.[variable.id] ?? [];
			return parameters.length > 0 ? { id: variable.id, name: variable.name, parameters: [...parameters] } : { id: variable.id, name: variable.name };
		});
	if (functions.length > 0) project.functions = functions;
	project.scripts = (state.blocks?.blocks ?? []).map((top) => ({
		x: Math.round(top.x ?? 0),
		y: Math.round(top.y ?? 0),
		blocks: chain(top).map(toProjectBlock),
	}));
	return project;
}

// --- project -> Blockly --------------------------------------------------------------

/** Inputs of each function of the project being converted (function id -> names). */
type Parameters = ReadonlyMap<string, string[]>;
const noParameters: Parameters = new Map();

function toBlockState(block: ProjectBlock, parameters: Parameters = noParameters): BlockState {
	const definition: CatalogBlock | undefined = findBlock(block.type);
	const state: BlockState = { type: block.type, id: block.id };
	const fields: Record<string, FieldState> = {};
	const inputs: NonNullable<BlockState["inputs"]> = {};

	for (const [name, value] of Object.entries(block.fields ?? {})) {
		const kind = definition?.args?.find((arg) => arg.name === name)?.kind;
		fields[name] = kind === "variable" || kind === "function" ? { id: value } : value;
	}
	// "My code" calls have only the slots of their function's parameters (ARG1..ARGn).
	const myCodeCall = block.type === "mycode_call" || block.type === "mycode_value";
	// A function call has the slots of its function's inputs, a definition shows their names.
	const functionBlock = block.type === FUNCTION_CALL || block.type === FUNCTION_DEFINE;
	const inputNames = functionBlock ? (parameters.get(block.fields?.FUNCTION ?? "") ?? []).slice(0, MAX_FUNCTION_PARAMETERS) : [];
	const params = myCodeCall
		? Object.keys(block.inputs ?? {}).filter((name) => /^ARG\d+$/.test(name)).length
		: inputNames.length;
	if (myCodeCall) state.extraState = { name: block.fields?.NAME ?? "", params };
	if (functionBlock) state.extraState = { params: inputNames };
	const slotted = myCodeCall || block.type === FUNCTION_CALL;
	if (definition) {
		for (const [name, { shadow }] of Object.entries(shadowInputs(definition))) {
			if (slotted && /^ARG\d+$/.test(name) && Number(name.slice(3)) > params) continue;
			inputs[name] = { shadow: structuredClone(shadow) as unknown as BlockState };
		}
	}
	for (const [name, input] of Object.entries(block.inputs ?? {})) {
		if (!definition?.args?.some((arg) => arg.name === name)) continue; // a slot this version does not have
		if (slotted && /^ARG\d+$/.test(name) && Number(name.slice(3)) > params) continue; // input removed since
		const target = (inputs[name] ??= {});
		const shadowFields = target.shadow?.fields;
		if (input.value !== undefined && shadowFields) {
			shadowFields[shadowFields.TEXT !== undefined ? "TEXT" : "NUM"] = input.value;
		}
		// A value block this version does not know is left out: the slot keeps its typed value.
		if (input.block && findBlock(input.block.type)) target.block = toBlockState(input.block, parameters);
	}
	for (const [name, stack] of Object.entries(block.statements ?? {})) {
		const first = linked(stack, parameters);
		if (first) inputs[name] = { block: first };
	}
	if (Object.keys(fields).length > 0) state.fields = fields;
	if (Object.keys(inputs).length > 0) state.inputs = inputs;
	return state;
}

function linked(stack: ProjectBlock[], parameters: Parameters = noParameters): BlockState | undefined {
	let next: BlockState | undefined;
	// A block this version does not know (made by a newer Scratch2Arduino) cannot be shown:
	// it is left out and the blocks below it move up, so the rest of the script still opens.
	for (const block of [...stack].reverse().filter((candidate) => findBlock(candidate.type))) {
		const state = toBlockState(block, parameters);
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

/** Block types of the project that this version does not know (they are not shown). */
export function unknownBlockTypes(project: Project): string[] {
	const unknown = new Set<string>();
	const visit = (block: ProjectBlock) => {
		if (!findBlock(block.type)) {
			unknown.add(block.type);
			return; // what is inside is left out with it
		}
		for (const input of Object.values(block.inputs ?? {})) if (input.block) visit(input.block);
		for (const stack of Object.values(block.statements ?? {})) stack.forEach(visit);
	};
	project.scripts.forEach((script) => script.blocks.forEach(visit));
	return [...unknown].sort();
}

/**
 * Variable and function fields pointing at nothing, or at the wrong kind (a "set" block holding
 * a function, saved by an older version), are left out: the block gets its default variable
 * instead of making Blockly invent one, which can fail and hide the whole project.
 */
function withValidReferences(project: Project): ProjectScript[] {
	const kinds = new Map<string, "variable" | "function">([
		...project.variables.map((variable) => [variable.id, "variable"] as const),
		...(project.functions ?? []).map((fn) => [fn.id, "function"] as const),
	]);
	const clean = (block: ProjectBlock): ProjectBlock => {
		const args = findBlock(block.type)?.args ?? [];
		let fields = block.fields;
		for (const [name, value] of Object.entries(block.fields ?? {})) {
			const kind = args.find((arg) => arg.name === name)?.kind;
			if ((kind === "variable" || kind === "function") && kinds.get(value) !== kind) {
				fields = { ...fields };
				delete fields[name];
			}
		}
		return {
			...block,
			...(fields ? { fields } : {}),
			...(block.inputs && {
				inputs: Object.fromEntries(
					Object.entries(block.inputs).map(([name, input]) => [name, input.block ? { ...input, block: clean(input.block) } : input])
				),
			}),
			...(block.statements && {
				statements: Object.fromEntries(Object.entries(block.statements).map(([name, stack]) => [name, stack.map(clean)])),
			}),
		};
	};
	return project.scripts.map((script) => ({ ...script, blocks: script.blocks.map(clean) }));
}

export function projectToWorkspace(project: Project): WorkspaceState {
	const blocks: BlockState[] = [];
	const parameters = new Map(
		(project.functions ?? [])
			.filter((fn) => (fn.parameters ?? []).length > 0)
			.map((fn) => [fn.id, (fn.parameters ?? []).slice(0, MAX_FUNCTION_PARAMETERS)] as const)
	);
	for (const script of withValidReferences(project)) {
		const overflow: ProjectBlock[][] = [];
		const first = linked(splitAfterTerminal(script.blocks, overflow), parameters);
		if (first) blocks.push({ ...first, x: script.x, y: script.y });
		overflow.forEach((stack, index) => {
			const loose = linked(stack, parameters);
			if (loose) blocks.push({ ...loose, x: script.x + 320, y: script.y + 60 * index });
		});
	}
	return {
		blocks: { languageVersion: 0, blocks },
		variables: [
			...project.variables.map((variable) => ({ id: variable.id, name: variable.name, type: "" })),
			...(project.functions ?? []).map((fn) => ({ id: fn.id, name: fn.name, type: FUNCTION_TYPE })),
		],
		...(parameters.size > 0 ? { functionParameters: Object.fromEntries(parameters) } : {}),
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
