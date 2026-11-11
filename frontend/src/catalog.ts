// Block catalog and board database, read at build time from the same JSON files as the
// C++ core (blocks/catalog, resources/boards): one source of truth with the C++ code generator.
export type Language = "fr" | "en";
export type Localized = Record<Language, string>;

export type BlockShape = "hat" | "statement" | "c" | "reporter" | "boolean";
export type ArgKind =
	| "number"
	| "text"
	| "boolean"
	| "pin"
	| "dropdown"
	| "variable"
	| "function"
	| "statement"
	| "usercode";
export type PinCapability = "digital" | "analog" | "pwm" | "interrupt";

export interface BlockArg {
	name: string;
	kind: ArgKind;
	default?: string | number;
	capability?: PinCapability;
	/** `tooltip`: shown over the menu when this option is chosen (the block keeps its own tooltip). */
	options?: { value: string; label: Localized; tooltip?: Localized }[];
}

export interface BlockDefinition {
	type: string;
	shape: BlockShape;
	terminal?: boolean;
	message: Localized;
	tooltip?: Localized;
	args?: BlockArg[];
}

export interface Category {
	id: string;
	colour: string;
	name: Localized;
	order: number;
}

export interface CatalogFile {
	category: Category;
	blocks: BlockDefinition[];
}

export interface Pin {
	label: string;
	capabilities: PinCapability[];
}

export interface Board {
	id: string;
	name: string;
	pins: Pin[];
	/** Live mode over Wi-Fi (ESP32, ESP8266). */
	wifi: boolean;
	/** Live mode over Bluetooth and the Phone blocks (ESP32). */
	bluetooth: boolean;
	/** Live mode over an HC-05 / HC-06 Bluetooth module wired to it (Arduino boards). */
	bluetoothModule: boolean;
}

export interface BoardFile {
	id: string;
	name: string;
	/**
	 * A count (pins 0..n-1), the list of usable GPIO numbers (ESP32), or the names printed on the
	 * board with their GPIO number (NodeMCU: { "D1": 5 }).
	 */
	digitalPins: number | number[] | Record<string, number>;
	/**
	 * Arduino boards: A0..A(count-1) numbered from firstNumber. GPIO boards: `pins` are digital
	 * pins that also read analog values, `inputOnly` are analog inputs named by their number.
	 */
	analogPins: { count?: number; firstNumber?: number; pins?: string[]; inputOnly?: string[] };
	pwmPins: string[];
	interruptPins: string[];
	wifi?: boolean;
	bluetooth?: boolean;
	bluetoothModule?: boolean;
}

export type CatalogBlock = BlockDefinition & { category: string };

export interface CatalogEntry {
	category: Category;
	blocks: CatalogBlock[];
}

export function buildCatalog(files: CatalogFile[]): CatalogEntry[] {
	return files
		.map((file) => ({
			category: file.category,
			blocks: file.blocks.map((block) => ({ ...block, category: file.category.id })),
		}))
		.sort((a, b) => a.category.order - b.category.order);
}

/** Mirrors BoardDatabase::fromJson in core/src/boards.cpp. */
export function buildBoard(file: BoardFile): Board {
	const numbered: { number: number; pin: Pin }[] = [];
	const digital: [string, number][] = Array.isArray(file.digitalPins)
		? file.digitalPins.map((number): [string, number] => [String(number), number])
		: typeof file.digitalPins === "number"
			? Array.from({ length: file.digitalPins }, (_, i): [string, number] => [String(i), i])
			: Object.entries(file.digitalPins);
	const analog = file.analogPins.pins ?? [];
	for (const [label, number] of digital) {
		const capabilities: PinCapability[] = ["digital"];
		if (analog.includes(label)) capabilities.push("analog");
		if (file.pwmPins.includes(label)) capabilities.push("pwm");
		if (file.interruptPins.includes(label)) capabilities.push("interrupt");
		numbered.push({ number, pin: { label, capabilities } });
	}
	const inputOnly = file.analogPins.inputOnly ?? [];
	if (file.analogPins.count !== undefined) {
		for (let i = 0; i < file.analogPins.count; ++i) {
			const label = `A${i}`;
			const capabilities: PinCapability[] = inputOnly.includes(label) ? ["analog"] : ["digital", "analog"];
			numbered.push({ number: (file.analogPins.firstNumber ?? 0) + i, pin: { label, capabilities } });
		}
	} else {
		for (const label of inputOnly) numbered.push({ number: Number(label), pin: { label, capabilities: ["analog"] } });
	}
	numbered.sort((a, b) => a.number - b.number);
	return {
		id: file.id,
		name: file.name,
		pins: numbered.map(({ pin }) => pin),
		wifi: file.wifi ?? false,
		bluetooth: file.bluetooth ?? false,
		bluetoothModule: file.bluetoothModule ?? false,
	};
}

const catalogFiles = import.meta.glob<CatalogFile>("../../blocks/catalog/*.json", {
	eager: true,
	import: "default",
});
const boardFiles = import.meta.glob<BoardFile>("../../resources/boards/*.json", {
	eager: true,
	import: "default",
});

export const catalog: CatalogEntry[] = buildCatalog(Object.values(catalogFiles));
export const boards: Board[] = Object.values(boardFiles)
	.map(buildBoard)
	.sort((a, b) => a.name.localeCompare(b.name));
export const defaultBoardId = "uno";

export function findBlock(type: string): CatalogBlock | undefined {
	for (const entry of catalog) {
		const found = entry.blocks.find((block) => block.type === type);
		if (found) return found;
	}
	return undefined;
}

export function findBoard(id: string): Board | undefined {
	return boards.find((board) => board.id === id);
}

/** Number/text shadows of a block's inputs, as Scratch shows them. */
export function shadowInputs(block: BlockDefinition): Record<string, { shadow: Record<string, unknown> }> {
	const inputs: Record<string, { shadow: Record<string, unknown> }> = {};
	for (const arg of block.args ?? []) {
		if (arg.kind === "number") {
			inputs[arg.name] = { shadow: { type: "math_number", fields: { NUM: String(arg.default ?? 0) } } };
		} else if (arg.kind === "text") {
			inputs[arg.name] = { shadow: { type: "text", fields: { TEXT: String(arg.default ?? "") } } };
		}
	}
	return inputs;
}
