import { act } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { FunctionDialog, type FunctionDraft } from "@/FunctionDialog";
import { MAX_FUNCTION_PARAMETERS } from "@/project/convert";
import { button, input, renderUi, typeInto, type Rendered } from "@/test/render";

let rendered: Rendered | null = null;
afterEach(() => {
	rendered?.unmount();
	rendered = null;
});

function show(initial: FunctionDraft | null, taken: string[] = []) {
	const onDone = vi.fn();
	const onCancel = vi.fn();
	rendered = renderUi(
		<FunctionDialog
			opened
			language="fr"
			initial={initial}
			nameTaken={(name) => taken.includes(name)}
			onCancel={onCancel}
			onDone={onDone}
		/>
	);
	return { onDone, onCancel };
}

// Audit #201: the function window had no test.
describe("FunctionDialog", () => {
	it("needs a name before OK", () => {
		const { onDone } = show(null);
		expect(button("Valider")?.disabled).toBe(true);
		typeInto(input("Nom de la fonction")!, "  biper  ");
		expect(button("Valider")?.disabled).toBe(false);
		act(() => button("Valider")!.click());
		expect(onDone).toHaveBeenCalledWith("biper", []);
	});

	it("refuses the name of another function, but keeps its own", () => {
		show({ name: "biper", inputs: [] }, ["biper", "clignoter"]);
		expect(button("Valider")?.disabled).toBe(false); // its own name
		typeInto(input("Nom de la fonction")!, "clignoter");
		expect(button("Valider")?.disabled).toBe(true);
		expect(document.body.textContent).toContain("Une autre fonction s'appelle déjà \"clignoter\".");
	});

	it("adds inputs up to the limit and gives them to onDone", () => {
		const { onDone } = show(null);
		typeInto(input("Nom de la fonction")!, "biper");
		for (let i = 0; i < MAX_FUNCTION_PARAMETERS; i += 1) act(() => button("Ajouter une entrée")!.click());
		expect(button("Ajouter une entrée")?.disabled).toBe(true);
		typeInto(input("Entrée 1")!, "fois");
		typeInto(input("Entrée 2")!, "durée");
		act(() => button("Valider")!.click());
		// Empty inputs are left out; new inputs come from nowhere (from: null).
		expect(onDone).toHaveBeenCalledWith("biper", [
			{ name: "fois", from: null },
			{ name: "durée", from: null },
		]);
	});

	it("keeps which input each one was when changing a function", () => {
		const { onDone } = show({ name: "biper", inputs: ["fois", "durée"] });
		act(() => [...document.querySelectorAll<HTMLButtonElement>("button[aria-label='Enlever cette entrée']")][0]!.click());
		act(() => button("Valider")!.click());
		expect(onDone).toHaveBeenCalledWith("biper", [{ name: "durée", from: 1 }]);
	});

	it("tells the editor when it is cancelled", () => {
		const { onCancel, onDone } = show(null);
		act(() => button("Annuler")!.click());
		expect(onCancel).toHaveBeenCalled();
		expect(onDone).not.toHaveBeenCalled();
	});
});
