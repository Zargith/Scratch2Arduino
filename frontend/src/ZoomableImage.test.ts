import { describe, expect, it } from "vitest";

import { clamp, FIT, MAX_ZOOM, zoomAt } from "@/ZoomableImage";

describe("diagram zoom", () => {
	it("keeps the point under the pointer in place", () => {
		const view = zoomAt(FIT, 2, 300, 200, 800, 500);
		expect(view).toEqual({ zoom: 2, x: -300, y: -200 });
		// The picture point under (300, 200) is still (300, 200) once zoomed.
		expect((300 - view.x) / view.zoom).toBe(300);
		expect((200 - view.y) / view.zoom).toBe(200);
	});

	it("never zooms out further than the whole picture, nor in beyond the limit", () => {
		expect(zoomAt(FIT, 0.5, 100, 100, 800, 500)).toEqual(FIT);
		expect(zoomAt(FIT, 50, 0, 0, 800, 500).zoom).toBe(MAX_ZOOM);
	});

	it("leaves no empty border when the picture is dragged", () => {
		expect(clamp({ zoom: 2, x: 100, y: 50 }, 800, 500)).toEqual({ zoom: 2, x: 0, y: 0 });
		expect(clamp({ zoom: 2, x: -2000, y: -900 }, 800, 500)).toEqual({ zoom: 2, x: -800, y: -500 });
		expect(clamp({ zoom: 2, x: -400, y: -100 }, 800, 500)).toEqual({ zoom: 2, x: -400, y: -100 });
	});

	it("zooms back out around the same point", () => {
		const zoomed = zoomAt(FIT, 3, 400, 250, 800, 500);
		expect(zoomAt(zoomed, 1, 400, 250, 800, 500)).toEqual(FIT);
	});
});
