// "My blocks": groups of blocks the user saved to reuse them in any project. A group is
// stored by the host (core/src/userdata.cpp) as a small project in the project.json format:
// its script and the variables and functions it uses, matched by name when it is reused.
import { findBlock } from "@/catalog";
import {
	emptyProject,
	projectToWorkspace,
	workspaceToProject,
	type BlockState,
	type Project,
	type ProjectBlock,
} from "@/project/convert";

/** Type of the Blockly variables that stand for functions (see blocks/definitions.ts). */
const FUNCTION_TYPE = "function";

export interface SavedGroup {
	id: string;
	name: string;
	blocks: Project;
}

export interface VariableInfo {
	id: string;
	name: string;
	/** "" for a variable, "function" for a function. */
	type?: string;
}

function visit(blocks: ProjectBlock[], action: (block: ProjectBlock) => void) {
	for (const block of blocks) {
		action(block);
		for (const input of Object.values(block.inputs ?? {})) if (input.block) visit([input.block], action);
		for (const stack of Object.values(block.statements ?? {})) visit(stack, action);
	}
}

/** Fields of the block holding a variable or a function id. */
function referenceFields(block: ProjectBlock): string[] {
	const args = findBlock(block.type)?.args ?? [];
	return Object.keys(block.fields ?? {}).filter((name) => {
		const kind = args.find((arg) => arg.name === name)?.kind;
		return kind === "variable" || kind === "function";
	});
}

/** How many blocks the group holds (shown next to its name). */
export function countBlocks(group: Project): number {
	let count = 0;
	for (const script of group.scripts) visit(script.blocks, () => (count += 1));
	return count;
}

/**
 * The stack starting at `top` (the block and the ones attached below it), as a group: only the
 * variables and functions its blocks use are kept.
 */
export function stackToGroup(top: BlockState, variables: VariableInfo[]): Project {
	const project = workspaceToProject(
		{ blocks: { languageVersion: 0, blocks: [{ ...top, x: 0, y: 0 }] }, variables },
		{ name: "", board: emptyProject("", "uno").board }
	);
	const used = new Set<string>();
	for (const script of project.scripts) {
		visit(script.blocks, (block) => {
			for (const field of referenceFields(block)) used.add(block.fields?.[field] ?? "");
		});
	}
	project.variables = project.variables.filter((variable) => used.has(variable.id)).map((variable) => ({ ...variable, initialValue: 0 }));
	const functions = (project.functions ?? []).filter((fn) => used.has(fn.id));
	if (functions.length > 0) project.functions = functions;
	else delete project.functions;
	return project;
}

export interface Insertion {
	/** Top blocks to add to the workspace (Blockly state, fresh ids, no position). */
	blocks: BlockState[];
	/** Variables and functions to create first: the project has none with these names. */
	create: Required<VariableInfo>[];
}

/**
 * Blocks of a group, ready to be added to a workspace whose variables and functions are
 * `existing`: those of the group are matched by name (same type), the missing ones are created.
 */
export function groupToBlocks(group: Project, existing: VariableInfo[], newId: () => string): Insertion {
	const ids = new Map<string, string>();
	const create: Required<VariableInfo>[] = [];
	const resolve = (id: string, name: string, type: string) => {
		const found = existing.find((candidate) => candidate.name === name && (candidate.type ?? "") === type);
		const planned = create.find((candidate) => candidate.name === name && candidate.type === type);
		const target = found?.id ?? planned?.id;
		if (target) {
			ids.set(id, target);
			return;
		}
		const created = { id: newId(), name, type };
		create.push(created);
		ids.set(id, created.id);
	};
	for (const variable of group.variables) resolve(variable.id, variable.name, "");
	for (const fn of group.functions ?? []) resolve(fn.id, fn.name, FUNCTION_TYPE);

	const scripts = structuredClone(group.scripts);
	for (const script of scripts) {
		visit(script.blocks, (block) => {
			block.id = newId();
			for (const field of referenceFields(block)) {
				const target = ids.get(block.fields?.[field] ?? "");
				// A reference the group does not describe (damaged file): the block keeps its default.
				if (target && block.fields) block.fields[field] = target;
				else if (block.fields) delete block.fields[field];
			}
		});
	}
	const state = projectToWorkspace({ ...emptyProject("", "uno"), scripts });
	const blocks = (state.blocks?.blocks ?? []).map(({ x: _x, y: _y, ...block }) => block);
	return { blocks, create };
}
