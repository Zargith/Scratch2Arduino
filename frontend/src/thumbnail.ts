// Thumbnail of a project (thumbnail.png in the .s2a file), shown in the recent projects list.
// Made on Save; any failure gives no thumbnail, never an error.
import type * as Blockly from "scratch-blocks";

/** Size of the thumbnail, in pixels. */
export const THUMBNAIL_WIDTH = 320;
export const THUMBNAIL_HEIGHT = 200;
const BACKGROUND = "#f9f9f9";

function canvas(): { canvas: HTMLCanvasElement; context: CanvasRenderingContext2D } | null {
	const element = document.createElement("canvas");
	element.width = THUMBNAIL_WIDTH;
	element.height = THUMBNAIL_HEIGHT;
	const context = element.getContext("2d");
	if (!context) return null;
	context.fillStyle = BACKGROUND;
	context.fillRect(0, 0, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
	return { canvas: element, context };
}

/** Where a picture of `width` x `height` is drawn to fit the thumbnail, centered, never enlarged above 1. */
export function fit(width: number, height: number, margin = 8): { x: number; y: number; scale: number } {
	if (width <= 0 || height <= 0) return { x: 0, y: 0, scale: 1 };
	const scale = Math.min((THUMBNAIL_WIDTH - 2 * margin) / width, (THUMBNAIL_HEIGHT - 2 * margin) / height, 1);
	return { x: (THUMBNAIL_WIDTH - width * scale) / 2, y: (THUMBNAIL_HEIGHT - height * scale) / 2, scale };
}

/** The first lines of a text program, as a PNG data URL. */
export function codeThumbnail(code: string): string | null {
	try {
		const drawing = canvas();
		if (!drawing) return null;
		const { context } = drawing;
		context.fillStyle = "#1f2933";
		context.font = "11px monospace";
		const lines = code.replace(/\t/g, "  ").split("\n").slice(0, 14);
		lines.forEach((line, index) => context.fillText(line.slice(0, 52), 10, 18 + index * 13));
		return drawing.canvas.toDataURL("image/png");
	} catch {
		return null;
	}
}

/** The CSS Blockly injected in the page: the blocks look the same once out of the page. */
function blocklyStyles(): string {
	return Array.from(document.querySelectorAll("style"))
		.filter((style) => style.id.startsWith("blockly") || style.textContent?.includes(".blockly"))
		.map((style) => style.textContent ?? "")
		.join("\n");
}

/** The blocks of the workspace, as a PNG data URL (null when there are none or on failure). */
export async function blocksThumbnail(workspace: Blockly.WorkspaceSvg): Promise<string | null> {
	try {
		const box = workspace.getBlocksBoundingBox();
		const width = box.right - box.left;
		const height = box.bottom - box.top;
		if (!(width > 0 && height > 0)) return null;
		const blocks = workspace.getCanvas().cloneNode(true) as SVGGElement;
		blocks.removeAttribute("transform");
		const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
		svg.setAttribute("xmlns", "http://www.w3.org/2000/svg");
		svg.setAttribute("width", String(width));
		svg.setAttribute("height", String(height));
		svg.setAttribute("viewBox", `${box.left} ${box.top} ${width} ${height}`);
		// The renderer classes sit on the injection div: keep them so the CSS selectors still match.
		const root = document.createElementNS("http://www.w3.org/2000/svg", "g");
		root.setAttribute("class", workspace.getInjectionDiv().className);
		const style = document.createElementNS("http://www.w3.org/2000/svg", "style");
		style.textContent = blocklyStyles();
		svg.append(style, root);
		const wrapper = document.createElementNS("http://www.w3.org/2000/svg", "g");
		wrapper.setAttribute("class", workspace.getParentSvg().getAttribute("class") ?? "");
		wrapper.append(blocks);
		root.append(wrapper);

		const image = new Image();
		await new Promise<void>((done, fail) => {
			image.onload = () => done();
			image.onerror = () => fail(new Error("SVG"));
			image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(new XMLSerializer().serializeToString(svg))}`;
		});
		const drawing = canvas();
		if (!drawing) return null;
		const place = fit(width, height);
		drawing.context.drawImage(image, place.x, place.y, width * place.scale, height * place.scale);
		return drawing.canvas.toDataURL("image/png");
	} catch {
		return null;
	}
}
