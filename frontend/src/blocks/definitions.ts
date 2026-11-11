// Turns the shared block catalog into scratch-blocks definitions, toolbox and theme.
import * as Blockly from "scratch-blocks";

import { translate } from "@/i18n";
import { catalog, shadowInputs, type Board, type CatalogBlock, type Language, type PinCapability } from "@/catalog";

export const CREATE_VARIABLE_CALLBACK = "S2A_CREATE_VARIABLE";
export const CREATE_FUNCTION_CALLBACK = "S2A_CREATE_FUNCTION";
export const MANAGE_LIBRARY_CALLBACK = "S2A_MANAGE_LIBRARY";
/** The button of an empty "My code" category: opens the code view. */
export const OPEN_CODE_CALLBACK = "S2A_OPEN_CODE";
/** Followed by the group id: the button that adds a "My blocks" group to the workspace. */
export const INSERT_GROUP_CALLBACK_PREFIX = "S2A_INSERT_GROUP:";
const LIBRARY_COLOUR = "#8D6E63";
/** Functions are Blockly variables of this type: named, renamed and deleted like variables. */
export const FUNCTION_TYPE = "function";

let currentBoard: Board | undefined;

/** A function or global variable of the user's "My code" file (core usercode::Declaration). */
export interface UserDeclaration {
	kind: "function" | "variable";
	name: string;
	parameters: string[];
	returnsValue: boolean;
	readOnly: boolean;
	/** Changed by a function of the code itself (its state): blocks read it, never set it. */
	changedByCode?: boolean;
	line: number;
}

let userDeclarations: UserDeclaration[] = [];

/** The "My code" blocks show the parameter names of these functions. */
export function setUserDeclarations(declarations: UserDeclaration[]): void {
	userDeclarations = declarations;
}

/** Number slots of a "My code" block: ARG1..ARGn (core usercode::kMaxParameters). */
export const MAX_PARAMETERS = 4;
export const MY_CODE_CALL = "mycode_call";
export const MY_CODE_VALUE = "mycode_value";


/** Pin menus list the pins of this board. */
export function setCurrentBoard(board: Board | undefined): void {
	currentBoard = board;
}

export function pinOptions(capability: PinCapability | undefined, current: string): [string, string][] {
	const pins = (currentBoard?.pins ?? []).filter(
		(pin) => capability === undefined || pin.capabilities.includes(capability)
	);
	const options: [string, string][] = pins.map((pin) => [pin.label, pin.label]);
	// Keep a pin that does not exist on the selected board: the validator reports it
	// on Build, the editor never refuses a value.
	if (current !== "" && !options.some(([, value]) => value === current)) {
		options.unshift([current, current]);
	}
	return options.length > 0 ? options : [["13", "13"]];
}

interface PinFieldConfig extends Blockly.FieldDropdownFromJsonConfig {
	capability?: PinCapability;
	value?: string;
}

type PinFieldFactory = (config: PinFieldConfig) => Blockly.Field;
let createPinField: PinFieldFactory | undefined;

/**
 * The pin field extends whatever class is registered as field_dropdown when the first
 * pin field is built: scratch-blocks replaces Blockly's dropdown during inject(), and
 * blocks are only instantiated after that.
 */
function pinFieldFactory(): PinFieldFactory {
	if (createPinField) return createPinField;
	const Dropdown = Blockly.registry.getClass(Blockly.registry.Type.FIELD, "field_dropdown") as unknown as typeof Blockly.FieldDropdown;

	class PinField extends Dropdown {
		constructor(capability: PinCapability | undefined, value: string) {
			super(function (this: Blockly.FieldDropdown) {
				return pinOptions(capability, String(this.getValue() ?? value));
			});
			this.setValue(value);
		}

		protected override doClassValidation_(newValue?: string): string | null {
			return typeof newValue === "string" && newValue !== "" ? newValue : null;
		}
	}
	createPinField = (config) => new PinField(config.capability, String(config.value ?? "13"));
	return createPinField;
}

function registerPinField(): void {
	if (Blockly.registry.hasItem(Blockly.registry.Type.FIELD, "field_s2a_pin")) return;
	const registrable = { fromJson: (config: PinFieldConfig) => pinFieldFactory()(config) };
	Blockly.fieldRegistry.register("field_s2a_pin", registrable as unknown as Blockly.fieldRegistry.RegistrableField);
}

/**
 * Menu of the function blocks: a variable menu (functions are typed variables) whose entries speak
 * of the function ("Rename the function", "Delete the function"), not of a variable.
 */
let functionLanguage: Language = "fr";

function registerFunctionField(): void {
	if (Blockly.registry.hasItem(Blockly.registry.Type.FIELD, "field_s2a_function")) return;
	const Base = Blockly.registry.getClass(Blockly.registry.Type.FIELD, "field_variable") as unknown as typeof Blockly.FieldVariable;
	class FunctionField extends Base {
		constructor(...args: ConstructorParameters<typeof Blockly.FieldVariable>) {
			super(...args);
			const self = this as unknown as { menuGenerator_: unknown };
			const original = self.menuGenerator_;
			self.menuGenerator_ = function (this: Blockly.FieldVariable): Blockly.MenuOption[] {
				const options = (typeof original === "function" ? original.call(this) : original) as Blockly.MenuOption[];
				return options.map((option) => {
					const id = (option as unknown[])[1];
					if (id === Blockly.RENAME_VARIABLE_ID) {
						return [translate(functionLanguage, "functions.rename"), id] as Blockly.MenuOption;
					}
					if (id === Blockly.DELETE_VARIABLE_ID) {
						return [translate(functionLanguage, "functions.delete", { name: this.getText() }), id] as Blockly.MenuOption;
					}
					return option;
				});
			};
		}

		protected override onItemSelected_(menu: Blockly.Menu, menuItem: Blockly.MenuItem): void {
			if (menuItem.getValue() !== Blockly.DELETE_VARIABLE_ID) {
				super.onItemSelected_(menu, menuItem);
				return;
			}
			// The confirmation ("delete its N uses?") is read while deleting: worded for a function.
			const saved = Blockly.Msg.DELETE_VARIABLE_CONFIRMATION;
			Blockly.Msg.DELETE_VARIABLE_CONFIRMATION = translate(functionLanguage, "functions.delete_confirm");
			try {
				super.onItemSelected_(menu, menuItem);
			} finally {
				Blockly.Msg.DELETE_VARIABLE_CONFIRMATION = saved;
			}
		}
	}
	Blockly.fieldRegistry.register("field_s2a_function", FunctionField as unknown as Blockly.fieldRegistry.RegistrableField);
}

type JsonArg = Record<string, unknown>;

function argJson(block: CatalogBlock, index: number, language: Language): JsonArg {
	const arg = block.args?.[index];
	if (!arg) return { type: "input_dummy" };
	switch (arg.kind) {
		case "number":
		case "text":
			return { type: "input_value", name: arg.name };
		case "boolean":
			return { type: "input_value", name: arg.name, check: "Boolean" };
		case "statement":
			return { type: "input_statement", name: arg.name };
		case "dropdown":
			return {
				type: "field_dropdown",
				name: arg.name,
				options: (arg.options ?? []).map((option) => [option.label[language], option.value]),
			};
		case "pin":
			return { type: "field_s2a_pin", name: arg.name, capability: arg.capability, value: String(arg.default ?? "13") };
		case "variable":
			// Only plain variables: functions are variables of another type and must not be listed.
			return { type: "field_variable", name: arg.name, variable: null, variableTypes: [""], defaultType: "" };
		case "usercode":
			return { type: "field_label_serializable", name: arg.name, text: "" };
		case "function":
			return {
				type: "field_s2a_function",
				name: arg.name,
				variable: null,
				variableTypes: [FUNCTION_TYPE],
				defaultType: FUNCTION_TYPE,
			};
		default:
			// A kind this editor does not know (catalog newer than the editor): an empty slot, never a crash.
			return { type: "input_dummy" };
	}
}

/** Blockly JSON: statement inputs go on their own line, as in Scratch. */
export function blockJson(block: CatalogBlock, language: Language): Record<string, unknown> {
	const json: Record<string, unknown> = { type: block.type, tooltip: block.tooltip?.[language] ?? "" };
	let messageIndex = 0;
	let text = "";
	let args: JsonArg[] = [];
	const flush = () => {
		if (text.trim() === "" && args.length === 0) return;
		json[`message${messageIndex}`] = text.trim();
		json[`args${messageIndex}`] = args;
		++messageIndex;
		text = "";
		args = [];
	};
	for (const part of block.message[language].split(/(%\d+)/)) {
		const placeholder = /^%(\d+)$/.exec(part);
		if (!placeholder) {
			text += part;
			continue;
		}
		const arg = argJson(block, Number(placeholder[1]) - 1, language);
		if (arg.type === "input_statement") {
			flush();
			json[`message${messageIndex}`] = "%1";
			json[`args${messageIndex}`] = [arg];
			++messageIndex;
		} else {
			args.push(arg);
			text += `%${args.length}`;
		}
	}
	flush();

	switch (block.shape) {
		case "hat":
			json.extensions = ["shape_hat"];
			break;
		case "reporter":
			json.extensions = ["output_number"];
			break;
		case "boolean":
			json.extensions = ["output_boolean"];
			break;
		default:
			json.extensions = [block.terminal ? "shape_end" : "shape_statement"];
	}
	return json;
}

interface MyCodeBlock extends Blockly.Block {
	params_?: number;
}

/** Gives a "call" / "result" block of "My code" its number slots, named after the parameters. */
function setParameters(block: MyCodeBlock, name: string, count: number): void {
	const names = userDeclarations.find((declaration) => declaration.kind === "function" && declaration.name === name)?.parameters;
	for (let i = count; i < (block.params_ ?? 0); i++) block.removeInput(`ARG${i + 1}`, true);
	for (let i = block.params_ ?? 0; i < Math.min(count, MAX_PARAMETERS); i++) {
		const input = block.appendValueInput(`ARG${i + 1}`);
		if (names?.[i]) input.appendField(names[i]);
	}
	block.params_ = Math.min(count, MAX_PARAMETERS);
}

/**
 * "My code" call and result blocks: as many slots as the function has parameters. The name and
 * the count are kept in the block's extra state (loaded before its fields).
 */
function defineMyCodeCalls(language: Language): void {
	for (const block of catalog.flatMap((entry) => entry.blocks)) {
		if (block.type !== MY_CODE_CALL && block.type !== MY_CODE_VALUE) continue;
		const statement = block.type === MY_CODE_CALL;
		Blockly.Blocks[block.type] = {
			init(this: MyCodeBlock) {
				this.jsonInit({
					type: block.type,
					message0: "%1",
					args0: [{ type: "field_label_serializable", name: "NAME", text: "" }],
					tooltip: block.tooltip?.[language] ?? "",
					extensions: [statement ? "shape_statement" : "output_number"],
				});
				this.setStyle(block.category);
				this.setInputsInline(true);
				this.params_ = 0;
			},
			saveExtraState(this: MyCodeBlock) {
				return { name: String(this.getFieldValue("NAME") ?? ""), params: this.params_ ?? 0 };
			},
			loadExtraState(this: MyCodeBlock, state: { name?: string; params?: number }) {
				setParameters(this, state.name ?? "", state.params ?? 0);
			},
		};
	}
}

export function defineBlocks(language: Language): void {
	registerPinField();
	registerFunctionField();
	functionLanguage = language;
	defineMyCodeCalls(language);
	for (const entry of catalog) {
		for (const block of entry.blocks) {
			if (block.type === MY_CODE_CALL || block.type === MY_CODE_VALUE) continue;
			const json = blockJson(block, language);
			Blockly.Blocks[block.type] = {
				init(this: Blockly.Block) {
					this.jsonInit(json);
					this.setStyle(block.category);
					// Menus whose options explain themselves (a note gives its frequency): the
					// pointer over the menu shows the chosen option, elsewhere the block's tooltip.
					for (const arg of block.args ?? []) {
						// Blockly takes the first option: start on the catalog default instead.
						if (arg.kind === "dropdown" && arg.default !== undefined) {
							this.getField(arg.name)?.setValue(String(arg.default));
						}
						const tips = new Map((arg.options ?? []).map((option) => [option.value, option.tooltip?.[language]]));
						const field = this.getField(arg.name);
						if (!field || ![...tips.values()].some(Boolean)) continue;
						field.setTooltip(() => tips.get(String(field.getValue())) ?? block.tooltip?.[language] ?? "");
					}
				},
			};
			if (NAMED_BY_LABEL.has(block.type)) {
				// The palette recycles blocks without extra state when it is rebuilt, and a recycled
				// block keeps its label: "lampe" showed "avant", and spare ones piled up at the top of
				// the palette. An (empty) extra state makes Blockly create these blocks afresh.
				Object.assign(Blockly.Blocks[block.type], {
					saveExtraState: () => null,
					loadExtraState: () => undefined,
				});
			}
		}
	}
}

/** Blocks whose name is a label set from the palette ("My code" variables): never recycled. */
const NAMED_BY_LABEL = new Set(["mycode_get", "mycode_set"]);

function shade(hex: string, factor: number): string {
	const value = Number.parseInt(hex.slice(1), 16);
	const channel = (shift: number) => Math.round(((value >> shift) & 0xff) * factor);
	return `#${[16, 8, 0].map((shift) => channel(shift).toString(16).padStart(2, "0")).join("")}`;
}

export function blocksTheme(): Blockly.Theme {
	const white = { colourPrimary: "#FFFFFF", colourSecondary: "#FFFFFF", colourTertiary: "#FFFFFF" };
	const blockStyles: Record<string, Blockly.Theme.BlockStyle> = { textField: { ...white, hat: "" } };
	const categoryStyles: Record<string, Blockly.Theme.CategoryStyle> = {};
	for (const { category } of catalog) {
		blockStyles[category.id] = {
			colourPrimary: category.colour,
			colourSecondary: shade(category.colour, 0.9),
			colourTertiary: shade(category.colour, 0.8),
			hat: "",
		};
		categoryStyles[category.id] = { colour: category.colour };
	}
	categoryStyles.library = { colour: LIBRARY_COLOUR };
	return Blockly.Theme.defineTheme("scratch2arduino", {
		name: "scratch2arduino",
		blockStyles,
		categoryStyles,
		componentStyles: {},
		fontStyle: {},
		startHats: true,
	});
}

/**
 * Palette. scratch-blocks leaves rebuilding the toolbox to the host (no dynamic categories):
 * the variables category is generated from the current variables, and the editor calls
 * updateToolbox() again when a variable is created, renamed or deleted.
 */
export function toolbox(
	language: Language,
	variables: { id: string }[],
	labels: { createVariable: string; createFunction: string },
	functions: { id: string }[] = [],
	library?: { title: string; manage: string; groups: { id: string; name: string }[] },
	myCode: { declarations: UserDeclaration[]; empty: string } = { declarations: [], empty: "" }
): Blockly.utils.toolbox.ToolboxDefinition {
	// Variables and functions: a "create" button, then the blocks set to the first one.
	const named = (blocks: CatalogBlock[], field: string, first: { id: string } | undefined, button: object) => [
		button,
		...(first
			? blocks.map((block) => ({
					kind: "block",
					type: block.type,
					fields: { [field]: { id: first.id } },
					inputs: shadowInputs(block),
				}))
			: []),
	];
	// "My blocks": one button per saved group (a click adds it to the workspace).
	const libraryCategory = library && {
		kind: "category",
		name: library.title,
		categorystyle: "library",
		contents: [
			{ kind: "button", text: library.manage, callbackkey: MANAGE_LIBRARY_CALLBACK },
			...library.groups.map((group) => ({
				kind: "button",
				text: `＋ ${group.name}`,
				callbackkey: `${INSERT_GROUP_CALLBACK_PREFIX}${group.id}`,
			})),
		],
	};
	return {
		kind: "categoryToolbox",
		contents: [...catalog.map(({ category, blocks }) => {
			let contents: object[];
			if (category.id === "variables") {
				contents = named(blocks, "VARIABLE", variables[0], {
					kind: "button",
					text: labels.createVariable,
					callbackkey: CREATE_VARIABLE_CALLBACK,
				});
			} else if (category.id === "functions") {
				contents = named(blocks, "FUNCTION", functions[0], {
					kind: "button",
					text: labels.createFunction,
					callbackkey: CREATE_FUNCTION_CALLBACK,
				});
			} else if (category.id === "mycode") {
				contents = myCodeContents(myCode.declarations, myCode.empty);
			} else {
				contents = blocks.map((block) => ({ kind: "block", type: block.type, inputs: shadowInputs(block) }));
			}
			return { kind: "category", name: category.name[language], categorystyle: category.id, contents };
		}), ...(libraryCategory ? [libraryCategory] : [])],
	} as Blockly.utils.toolbox.ToolboxDefinition;
}

/** Number slots of the "My code" blocks, as the palette shows them. */
function numberShadows(names: string[]): Record<string, { shadow: Record<string, unknown> }> {
	return Object.fromEntries(names.map((name) => [name, { shadow: { type: "math_number", fields: { NUM: "0" } } }]));
}

/** Palette of the "My code" category: blocks for the functions and variables of the user's file. */
export function myCodeContents(declarations: UserDeclaration[], empty: string): object[] {
	if (declarations.length === 0) return [{ kind: "button", text: empty, callbackkey: OPEN_CODE_CALLBACK }];
	const contents: object[] = [];
	for (const declaration of declarations) {
		if (declaration.kind === "function") {
			const params = Math.min(declaration.parameters.length, MAX_PARAMETERS);
			const slots = numberShadows(Array.from({ length: params }, (_, i) => `ARG${i + 1}`));
			const extraState = { name: declaration.name, params };
			contents.push({ kind: "block", type: MY_CODE_CALL, fields: { NAME: declaration.name }, extraState, inputs: slots });
			if (declaration.returnsValue) {
				contents.push({ kind: "block", type: MY_CODE_VALUE, fields: { NAME: declaration.name }, extraState, inputs: slots });
			}
		} else {
			contents.push({ kind: "block", type: "mycode_get", fields: { NAME: declaration.name } });
			// Only variables that blocks may change: not the constants, not the code's own state.
			if (!declaration.readOnly && !declaration.changedByCode) {
				contents.push({ kind: "block", type: "mycode_set", fields: { NAME: declaration.name }, inputs: numberShadows(["VALUE"]) });
			}
		}
	}
	return contents;
}
