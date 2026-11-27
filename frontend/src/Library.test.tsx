import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { translate, type MessageKey } from "@/i18n";
import { Library } from "@/Library";
import type { SavedGroup } from "@/myBlocks";
import { button, input, renderUi, typeInto, type Rendered } from "@/test/render";

const group: SavedGroup = {
	id: "g1",
	name: "Clignoter",
	blocks: {
		format: "scratch2arduino-project",
		formatVersion: 2,
		name: "Clignoter",
		board: "uno",
		variables: [],
		scripts: [{ x: 0, y: 0, blocks: [{ id: "a", type: "pins_builtin_led", fields: { STATE: "ON" } }] }],
	},
};

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
	vi.restoreAllMocks();
});

function show(groups: SavedGroup[]) {
	const handlers = {
		onInsert: vi.fn(),
		onRename: vi.fn(),
		onRemove: vi.fn(),
		onExport: vi.fn(),
		onImport: vi.fn(),
		onClose: vi.fn(),
	};
	const t = (key: MessageKey, params?: Record<string, string>) => translate("fr", key, params);
	rendered = renderUi(<Library opened groups={groups} t={t} {...handlers} />);
	return handlers;
}

// Audit #201: "My blocks" had no test.
describe("Library", () => {
	it("says it is empty and cannot export nothing", () => {
		show([]);
		expect(document.body.textContent).toContain(translate("fr", "library.empty"));
		expect(button("Exporter")?.disabled).toBe(true);
	});

	it("adds a saved group to the project", () => {
		const { onInsert } = show([group]);
		act(() => button("Ajouter au projet")!.click());
		expect(onInsert).toHaveBeenCalledWith(group);
	});

	it("renames a group when its name is left, but never to an empty name", () => {
		const { onRename } = show([group]);
		const name = input("Nom du groupe")!;
		typeInto(name, "  Clignoter vite ");
		act(() => name.dispatchEvent(new FocusEvent("focusout", { bubbles: true })));
		expect(onRename).toHaveBeenCalledWith(group, "Clignoter vite");

		onRename.mockClear();
		typeInto(name, "   ");
		act(() => name.dispatchEvent(new FocusEvent("focusout", { bubbles: true })));
		expect(onRename).not.toHaveBeenCalled();
		expect(name.value).toBe("Clignoter");
	});

	it("deletes a group only once the user agrees", () => {
		const { onRemove } = show([group]);
		const confirm = vi.spyOn(window, "confirm").mockReturnValueOnce(false).mockReturnValueOnce(true);
		act(() => button("Supprimer")!.click());
		expect(onRemove).not.toHaveBeenCalled();
		act(() => button("Supprimer")!.click());
		expect(onRemove).toHaveBeenCalledWith(group);
		expect(confirm).toHaveBeenCalledTimes(2);
	});

	it("exports and imports", () => {
		const { onExport, onImport } = show([group]);
		act(() => button("Exporter")!.click());
		act(() => button("Importer")!.click());
		expect(onExport).toHaveBeenCalled();
		expect(onImport).toHaveBeenCalled();
	});
});
