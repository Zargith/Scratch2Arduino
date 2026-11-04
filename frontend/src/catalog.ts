// Block catalog and board database, read at build time from the same JSON files as the
// C++ core (blocks/catalog, resources/boards): one source of truth for both variants.
export type Language = "fr" | "en";
export type Localized = Record<Language, string>;

export type BlockShape = "hat" | "statement" | "c" | "reporter" | "boolean";
export type ArgKind = "number" | "text" | "boolean" | "pin" | "dropdown" | "variable" | "statement";
export type PinCapability = "digital" | "analog" | "pwm" | "interrupt";

export interface BlockArg {
	name: string;
	kind: ArgKind;
	default?: string | number;
	capability?: PinCapability;
	options?: { value: string; label: Localized }[];
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
}

export interface BoardFile {
	id: string;
	name: string;
	digitalPins: number;
	analogPins: { count: number; firstNumber: number; inputOnly?: string[] };
	pwmPins: string[];
	interruptPins: string[];
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
	const pins: Pin[] = [];
	for (let i = 0; i < file.digitalPins; ++i) {
		const label = String(i);
		const capabilities: PinCapability[] = ["digital"];
		if (file.pwmPins.includes(label)) capabilities.push("pwm");
		if (file.interruptPins.includes(label)) capabilities.push("interrupt");
		pins.push({ label, capabilities });
	}
	for (let i = 0; i < file.analogPins.count; ++i) {
		const label = `A${i}`;
		const inputOnly = (file.analogPins.inputOnly ?? []).includes(label);
		pins.push({ label, capabilities: inputOnly ? ["analog"] : ["digital", "analog"] });
	}
	return { id: file.id, name: file.name, pins };
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
