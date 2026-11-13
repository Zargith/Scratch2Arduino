// Functions with inputs: the "define" and "call" blocks, the input blocks, and what keeps them in
// step with the inputs of each function (kept here, saved with the workspace).
//
// A function is a Blockly variable of type "function" (named, renamed, deleted like a variable).
// Its inputs are numbers, named by the user: a "call" block has one slot per input (ARG1..ARG4),
// in order, and the "define" block shows their names. Inside the function, an input block
// ("functions_parameter", named by its NAME label) gives the number of the slot.
import * as Blockly from "scratch-blocks";

import { FUNCTION_CALL, FUNCTION_DEFINE, MAX_FUNCTION_PARAMETERS } from "@/project/convert";

export const FUNCTION_PARAMETER = "functions_parameter";
/** Key of the inputs in the saved workspace state (WorkspaceState.functionParameters). */
export const PARAMETERS_SERIALIZER = "functionParameters";

/** Function id -> names of its inputs. Only one editor exists at a time. */
let parameters = new Map<string, string[]>();

export function functionParameters(id: string): string[] {
	return parameters.get(id) ?? [];
}

export function setFunctionParameters(id: string, names: string[]): void {
	const kept = names.slice(0, MAX_FUNCTION_PARAMETERS);
	if (kept.length > 0) parameters.set(id, kept);
	else parameters.delete(id);
}

/** Every function with inputs, for the saved state and the "My blocks" groups. */
export function allFunctionParameters(): Record<string, string[]> {
	return Object.fromEntries([...parameters].map(([id, names]) => [id, [...names]]));
}

/**
 * Names typed in the function window, made usable: trimmed, empty ones left out, a name given
 * twice gets a number ("n", "n 2"), at most MAX_FUNCTION_PARAMETERS.
 */
export function cleanParameterNames(names: string[]): string[] {
	const result: string[] = [];
	for (const typed of names) {
		const name = typed.trim().replace(/\s+/g, " ");
		if (name === "") continue;
		let unique = name;
		for (let n = 2; result.includes(unique); n++) unique = `${name} ${n}`;
		result.push(unique);
		if (result.length === MAX_FUNCTION_PARAMETERS) break;
	}
	return result;
}

interface FunctionBlock extends Blockly.Block {
	/** Names of the inputs the block shows now. */
	params_?: string[];
}

const same = (a: readonly string[], b: readonly string[]) => a.length === b.length && a.every((name, i) => name === b[i]);

/** Separator inputs: an empty end-of-row input puts what follows on the next line. */
const BREAK = "S2A_BREAK";
const NAMES = "S2A_NAMES";

function removeShapeInputs(block: Blockly.Block): void {
	for (const input of [...block.inputList]) {
		if (input.name.startsWith(BREAK) || input.name === NAMES) block.removeInput(input.name, true);
	}
}

/**
 * The slots of a "call" block, one per input: on the same line for a single input, one line
 * each for more (the block grows taller instead of wider). Blocks already in the slots stay.
 */
function shapeCall(block: FunctionBlock, names: string[], addShadows: boolean): void {
	const before = block.params_ ?? [];
	removeShapeInputs(block);
	for (let i = names.length; i < before.length; i++) block.removeInput(`ARG${i + 1}`, true);
	const multiline = names.length > 1;
	names.forEach((name, i) => {
		const slot = `ARG${i + 1}`;
		let input = block.getInput(slot);
		if (!input) {
			input = block.appendValueInput(slot);
			if (addShadows) input.connection?.setShadowState({ type: "math_number", fields: { NUM: "0" } });
		}
		// The label is rebuilt: an input may have been renamed.
		const label = `${slot}_LABEL`;
		if (input.fieldRow.some((field) => field.name === label)) input.removeField(label, true);
		if (name !== "") input.insertFieldAt(0, name, label);
		if (multiline) {
			// After the function menu, then after each slot: the next one goes on a new line.
			block.appendEndRowInput(`${BREAK}${i}`);
			block.moveInputBefore(`${BREAK}${i}`, slot);
		}
	});
	block.params_ = [...names];
}

/** The "define" hat shows the names of the inputs on a line of their own. */
function shapeDefine(block: FunctionBlock, names: string[], label: string): void {
	removeShapeInputs(block);
	if (names.length > 0) {
		block.appendEndRowInput(`${BREAK}0`);
		block.appendDummyInput(NAMES).appendField(`${label} ${names.join(", ")}`);
	}
	block.params_ = [...names];
}

/** Words of the blocks in the editor's language. */
let inputsWord = "inputs:";

/**
 * Defines the "call" and "define" blocks from their catalog JSON (built by blockJson): the call
 * keeps the first line (its word and the function menu), the slots are added by shapeCall.
 */
export function defineFunctionBlocks(call: Record<string, unknown>, define: Record<string, unknown>, inputs: string): void {
	inputsWord = inputs;
	const shaped = (json: Record<string, unknown>, shape: (block: FunctionBlock, names: string[], addShadows: boolean) => void) => ({
		init(this: FunctionBlock) {
			this.jsonInit(json);
			this.setStyle("functions");
			this.setInputsInline(true);
			this.params_ = [];
		},
		saveExtraState(this: FunctionBlock) {
			return { params: [...(this.params_ ?? [])] };
		},
		loadExtraState(this: FunctionBlock, state: { params?: unknown }) {
			const names = Array.isArray(state?.params) ? state.params.filter((name): name is string => typeof name === "string") : [];
			shape(this, names.slice(0, MAX_FUNCTION_PARAMETERS), false);
		},
		onchange(this: FunctionBlock, event: Blockly.Events.Abstract) {
			// Another function picked in the menu: the block takes its inputs.
			const change = event as Blockly.Events.BlockChange;
			if (event.type !== Blockly.Events.BLOCK_CHANGE || change.blockId !== this.id || change.name !== "FUNCTION") return;
			syncFunctionBlock(this);
		},
	});
	const callJson: Record<string, unknown> = {};
	for (const [key, value] of Object.entries(call)) if (!/^(message|args)\d+$/.test(key)) callJson[key] = value;
	callJson.message0 = String(call.message0).split("%2")[0]?.trim() ?? "%1";
	callJson.args0 = ((call.args0 as unknown[] | undefined) ?? []).slice(0, 1);
	Blockly.Blocks[FUNCTION_CALL] = shaped(callJson, shapeCall);
	Blockly.Blocks[FUNCTION_DEFINE] = shaped(define, (block, names) => shapeDefine(block, names, inputsWord));
}

/** Gives a "call" or "define" block the inputs of the function it shows; true when it changed. */
export function syncFunctionBlock(block: Blockly.Block): boolean {
	const shaped = block as FunctionBlock;
	if (block.type !== FUNCTION_CALL && block.type !== FUNCTION_DEFINE) return false;
	const names = functionParameters(String(block.getFieldValue("FUNCTION") ?? ""));
	if (same(shaped.params_ ?? [], names)) return false;
	if (block.type === FUNCTION_CALL) shapeCall(shaped, names, true);
	else shapeDefine(shaped, names, inputsWord);
	if (block instanceof Blockly.BlockSvg && block.rendered) block.queueRender();
	return true;
}

/** Every "call" and "define" block of the workspace in step with the inputs of its function. */
export function syncFunctionBlocks(workspace: Blockly.Workspace): void {
	for (const block of workspace.getAllBlocks(false)) syncFunctionBlock(block);
}

/** One input of the function window: its name, and the input it was before (null: new). */
export interface EditedParameter {
	name: string;
	from: number | null;
}

interface HeldSlot {
	plugged: Blockly.Block | null;
	shadow: Blockly.serialization.blocks.State | null;
	used: boolean;
}

/**
 * Changes the inputs of a function: the "call" blocks keep what was in the slot of each input
 * that stays (even moved), the input blocks inside the function follow a renamed input. Blocks
 * that were in the slot of a removed input are left beside the call (they still exist).
 */
export function changeFunctionParameters(workspace: Blockly.Workspace, id: string, edited: EditedParameter[]): void {
	const before = functionParameters(id);
	const kept = edited.slice(0, MAX_FUNCTION_PARAMETERS);
	setFunctionParameters(
		id,
		kept.map((entry) => entry.name)
	);
	const renamed = new Map<string, string>();
	for (const entry of kept) {
		const old = entry.from === null ? undefined : before[entry.from];
		if (old !== undefined && old !== entry.name) renamed.set(old, entry.name);
	}
	for (const block of workspace.getAllBlocks(false).filter((candidate) => candidate.getFieldValue("FUNCTION") === id)) {
		if (block.type === FUNCTION_DEFINE) {
			syncFunctionBlock(block);
			// The input blocks of the function follow their input's new name.
			for (const inner of block.getDescendants(false)) {
				const next = inner.type === FUNCTION_PARAMETER ? renamed.get(String(inner.getFieldValue("NAME") ?? "")) : undefined;
				if (next !== undefined) inner.setFieldValue(next, "NAME");
			}
			continue;
		}
		if (block.type !== FUNCTION_CALL) continue;
		// What each slot holds: a block plugged in, or the number typed in its shadow.
		const held: HeldSlot[] = before.map((_, i) => {
			const connection = block.getInput(`ARG${i + 1}`)?.connection;
			const target = connection?.targetBlock() ?? null;
			const plugged = target && !target.isShadow() ? target : null;
			const shadow = connection?.getShadowState(true) ?? null;
			plugged?.unplug(false);
			return { plugged, shadow, used: false };
		});
		for (let i = 0; i < before.length; i++) block.removeInput(`ARG${i + 1}`, true);
		(block as FunctionBlock).params_ = [];
		shapeCall(block as FunctionBlock, functionParameters(id), true);
		kept.forEach((entry, i) => {
			const previous = entry.from === null ? undefined : held[entry.from];
			const connection = block.getInput(`ARG${i + 1}`)?.connection;
			if (!previous || !connection) return;
			if (previous.shadow) connection.setShadowState(previous.shadow);
			if (previous.plugged?.outputConnection) connection.connect(previous.plugged.outputConnection);
			previous.used = true;
		});
		// Blocks of removed inputs: moved a little beside the call, so they are seen.
		for (const slot of held) {
			if (slot.plugged && !slot.used && slot.plugged instanceof Blockly.BlockSvg) slot.plugged.moveBy(40, 40);
		}
		if (block instanceof Blockly.BlockSvg && block.rendered) block.queueRender();
	}
}

/** Saves the inputs of the functions with the workspace, and reads them back before the blocks. */
export function registerParametersSerializer(): void {
	if (Blockly.registry.hasItem(Blockly.registry.Type.SERIALIZER, PARAMETERS_SERIALIZER)) return;
	Blockly.serialization.registry.register(PARAMETERS_SERIALIZER, {
		// After the variables (100), before the blocks (50).
		priority: 90,
		save: (workspace: Blockly.Workspace) => {
			const ids = new Set(workspace.getVariableMap().getAllVariables().map((variable) => variable.getId()));
			const saved = Object.fromEntries(Object.entries(allFunctionParameters()).filter(([id]) => ids.has(id)));
			return Object.keys(saved).length > 0 ? saved : null;
		},
		load: (state: unknown) => {
			parameters = new Map();
			if (typeof state !== "object" || state === null) return;
			for (const [id, names] of Object.entries(state as Record<string, unknown>)) {
				if (Array.isArray(names)) setFunctionParameters(id, names.filter((name): name is string => typeof name === "string"));
			}
		},
		clear: () => {
			parameters = new Map();
		},
	});
}

/**
 * Palette of the functions category after its "create" button: define and call set to the first
 * function, then the input blocks of every function, under the name of their function.
 */
export function functionPalette(functions: { id: string; name: string }[], inputsOf: (name: string) => string): object[] {
	const first = functions[0];
	if (!first) return [];
	const names = functionParameters(first.id);
	const slots = Object.fromEntries(names.map((_, i) => [`ARG${i + 1}`, { shadow: { type: "math_number", fields: { NUM: "0" } } }]));
	const contents: object[] = [
		{ kind: "block", type: FUNCTION_DEFINE, fields: { FUNCTION: { id: first.id } }, extraState: { params: names } },
		{ kind: "block", type: FUNCTION_CALL, fields: { FUNCTION: { id: first.id } }, extraState: { params: names }, inputs: slots },
	];
	for (const fn of functions) {
		const inputs = functionParameters(fn.id);
		if (inputs.length === 0) continue;
		contents.push({ kind: "label", text: inputsOf(fn.name) });
		for (const name of inputs) contents.push({ kind: "block", type: FUNCTION_PARAMETER, fields: { NAME: name } });
	}
	return contents;
}
