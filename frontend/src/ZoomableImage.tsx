import { ActionIcon, Box, Group, Text, Tooltip } from "@mantine/core";
import { IconZoomIn, IconZoomOut, IconZoomReset } from "@tabler/icons-react";
import { useCallback, useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from "react";

import type { MessageKey } from "@/i18n";

export const MIN_ZOOM = 1;
export const MAX_ZOOM = 6;
const STEP = 1.5;

export interface View {
	zoom: number;
	/** Position of the picture's top-left corner in the frame, in pixels (≤ 0 once zoomed). */
	x: number;
	y: number;
}

export const FIT: View = { zoom: 1, x: 0, y: 0 };

/** Keeps the zoomed picture over the whole frame (no empty border when dragging). */
export function clamp(view: View, width: number, height: number): View {
	const limit = (value: number, size: number) => Math.min(0, Math.max(size - size * view.zoom, value));
	return { zoom: view.zoom, x: limit(view.x, width), y: limit(view.y, height) };
}

/**
 * The view after zooming to `zoom` around the point (px, py) of a frame of `width` × `height`:
 * that point stays under the pointer, and the picture always covers the frame.
 */
export function zoomAt(view: View, zoom: number, px: number, py: number, width: number, height: number): View {
	const next = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom));
	const ratio = next / view.zoom;
	return clamp({ zoom: next, x: px - (px - view.x) * ratio, y: py - (py - view.y) * ratio }, width, height);
}

interface ZoomableImageProps {
	src: string;
	alt: string;
	/** Height of the frame (CSS): the picture fits inside it at zoom 1. */
	height: string;
	t(key: MessageKey, params?: Record<string, string>): string;
}

/** A picture to look at closely: wheel or buttons to zoom, drag to move, double-click to zoom in. */
export function ZoomableImage({ src, alt, height, t }: ZoomableImageProps) {
	const frame = useRef<HTMLDivElement>(null);
	const drag = useRef<{ pointer: number; x: number; y: number } | null>(null);
	const [view, setView] = useState<View>(FIT);
	const [dragging, setDragging] = useState(false);

	useEffect(() => setView(FIT), [src]);

	const measure = () => {
		const box = frame.current?.getBoundingClientRect();
		return { width: box?.width ?? 0, height: box?.height ?? 0, left: box?.left ?? 0, top: box?.top ?? 0 };
	};
	/** Zoom around the centre of the frame (buttons). */
	const zoomCentre = (zoom: number) => {
		const box = measure();
		setView((current) => zoomAt(current, zoom, box.width / 2, box.height / 2, box.width, box.height));
	};

	// The wheel zooms: a native listener, so that the dialog does not scroll at the same time.
	const onWheel = useCallback((event: WheelEvent) => {
		event.preventDefault();
		const box = measure();
		const factor = event.deltaY < 0 ? 1.2 : 1 / 1.2;
		setView((current) =>
			zoomAt(current, current.zoom * factor, event.clientX - box.left, event.clientY - box.top, box.width, box.height)
		);
	}, []);
	useEffect(() => {
		const element = frame.current;
		if (!element) return undefined;
		element.addEventListener("wheel", onWheel, { passive: false });
		return () => element.removeEventListener("wheel", onWheel);
	}, [onWheel]);

	const onPointerDown = (event: ReactPointerEvent<HTMLDivElement>) => {
		if (view.zoom <= MIN_ZOOM) return;
		drag.current = { pointer: event.pointerId, x: event.clientX, y: event.clientY };
		setDragging(true);
		event.currentTarget.setPointerCapture?.(event.pointerId);
	};
	const onPointerMove = (event: ReactPointerEvent<HTMLDivElement>) => {
		const start = drag.current;
		if (!start || start.pointer !== event.pointerId) return;
		const dx = event.clientX - start.x;
		const dy = event.clientY - start.y;
		drag.current = { ...start, x: event.clientX, y: event.clientY };
		const box = measure();
		setView((current) => clamp({ ...current, x: current.x + dx, y: current.y + dy }, box.width, box.height));
	};
	const onPointerUp = () => {
		drag.current = null;
		setDragging(false);
	};

	const zoomed = view.zoom > MIN_ZOOM;
	return (
		<Box>
			<Group gap="xs" mb="xs">
				<Tooltip label={t("zoom.out")}>
					<ActionIcon
						variant="default"
						aria-label={t("zoom.out")}
						disabled={!zoomed}
						onClick={() => zoomCentre(view.zoom / STEP)}
					>
						<IconZoomOut size={18} />
					</ActionIcon>
				</Tooltip>
				<Text size="sm" w={52} ta="center" aria-live="polite">
					{Math.round(view.zoom * 100)} %
				</Text>
				<Tooltip label={t("zoom.in")}>
					<ActionIcon
						variant="default"
						aria-label={t("zoom.in")}
						disabled={view.zoom >= MAX_ZOOM}
						onClick={() => zoomCentre(view.zoom * STEP)}
					>
						<IconZoomIn size={18} />
					</ActionIcon>
				</Tooltip>
				<Tooltip label={t("zoom.fit")}>
					<ActionIcon variant="default" aria-label={t("zoom.fit")} disabled={!zoomed} onClick={() => setView(FIT)}>
						<IconZoomReset size={18} />
					</ActionIcon>
				</Tooltip>
				<Text size="xs" c="dimmed">
					{t("zoom.help")}
				</Text>
			</Group>
			<Box
				ref={frame}
				h={height}
				onPointerDown={onPointerDown}
				onPointerMove={onPointerMove}
				onPointerUp={onPointerUp}
				onPointerCancel={onPointerUp}
				onDoubleClick={(event) => {
					const box = measure();
					setView((current) =>
						zoomAt(current, current.zoom * 2, event.clientX - box.left, event.clientY - box.top, box.width, box.height)
					);
				}}
				style={{
					overflow: "hidden",
					position: "relative",
					borderRadius: 4,
					cursor: zoomed ? (dragging ? "grabbing" : "grab") : "zoom-in",
					touchAction: "none",
					userSelect: "none",
				}}
			>
				<img
					src={src}
					alt={alt}
					draggable={false}
					style={{
						width: "100%",
						height: "100%",
						objectFit: "contain",
						transformOrigin: "0 0",
						transform: `translate(${view.x}px, ${view.y}px) scale(${view.zoom})`,
						display: "block",
					}}
				/>
			</Box>
		</Box>
	);
}
