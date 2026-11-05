// Turns the shared block catalog into scratch-blocks definitions, toolbox and theme.
import * as Blockly from "scratch-blocks";

import { catalog, shadowInputs, type Board, type CatalogBlock, type Language, type PinCapability } from "@/catalog";

export const CREATE_VARIABLE_CALLBACK = "S2A_CREATE_VARIABLE";

let currentBoard: Board | undefined;

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
			return { type: "field_variable", name: arg.name, variable: null };
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

export function defineBlocks(language: Language): void {
	registerPinField();
	for (const entry of catalog) {
		for (const block of entry.blocks) {
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
		}
	}
}

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
	createVariableLabel: string
): Blockly.utils.toolbox.ToolboxDefinition {
	return {
		kind: "categoryToolbox",
		contents: catalog.map(({ category, blocks }) => {
			const first = variables[0];
			const contents =
				category.id === "variables"
					? [
							{ kind: "button", text: createVariableLabel, callbackkey: CREATE_VARIABLE_CALLBACK },
							...(first
								? blocks.map((block) => ({
										kind: "block",
										type: block.type,
										fields: { VARIABLE: { id: first.id } },
										inputs: shadowInputs(block),
									}))
								: []),
						]
					: blocks.map((block) => ({ kind: "block", type: block.type, inputs: shadowInputs(block) }));
			return { kind: "category", name: category.name[language], categorystyle: category.id, contents };
		}),
	} as Blockly.utils.toolbox.ToolboxDefinition;
}
