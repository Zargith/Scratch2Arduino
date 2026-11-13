// Card of the interactive tutorial: the current step, a ring around what to use, and the Next
// button that turns on once the step sees that it is done.
import { ActionIcon, Badge, Button, Group, Paper, Progress, Stack, Text, Title } from "@mantine/core";
import { IconCheck, IconChevronDown, IconChevronUp, IconX } from "@tabler/icons-react";
import { useEffect, useRef, useState } from "react";

import { catalog, type Language } from "@/catalog";
import { translate, type MessageKey } from "@/i18n";
import { steps, type TourContext, type TourStep, type TourTarget } from "@/tour/steps";
import "@/tour/Tour.css";

interface TourProps {
	language: Language;
	context: TourContext;
	/** Index of the step shown. */
	step: number;
	onStep(step: number): void;
	/** The card's button of a step (writing the tune in "My code"). */
	onAction(action: NonNullable<TourStep["action"]>["run"]): void;
	onClose(): void;
}

/** The element a step points at, if it is on screen. */
export function findTarget(target: TourTarget | undefined, language: Language, root: ParentNode = document): HTMLElement | null {
	if (!target) return null;
	if (target.kind === "label") {
		const label = translate(language, target.key as MessageKey);
		return root.querySelector<HTMLElement>(`[aria-label="${CSS.escape(label)}"]`);
	}
	const text =
		target.kind === "text" ? target.text[language] : (catalog.find((entry) => entry.category.id === target.id)?.category.name[language] ?? "");
	const candidates =
		target.kind === "text"
			? root.querySelectorAll<HTMLElement>("button")
			: root.querySelectorAll<HTMLElement>(".scratchCategoryMenuItem, [role='treeitem']");
	return [...candidates].find((element) => element.textContent?.trim() === text && element.getClientRects().length > 0) ?? null;
}

/** Position of the ring around the target, followed while the window changes. */
function useRing(target: TourTarget | undefined, language: Language): DOMRect | null {
	const [rect, setRect] = useState<DOMRect | null>(null);
	useEffect(() => {
		const update = () => {
			const found = findTarget(target, language)?.getBoundingClientRect() ?? null;
			setRect((current) =>
				current && found && current.x === found.x && current.y === found.y && current.width === found.width && current.height === found.height
					? current
					: found
			);
		};
		update();
		const timer = window.setInterval(update, 400);
		return () => window.clearInterval(timer);
	}, [target, language]);
	return rect;
}

export function Tour({ language, context, step, onStep, onAction, onClose }: TourProps) {
	const t = (key: MessageKey, params?: Record<string, string>) => translate(language, key, params);
	const current = steps[Math.min(step, steps.length - 1)]!;
	const [folded, setFolded] = useState(false);
	/** What the step saw since it started, and the editor when it started. */
	const memory = useRef<Record<string, unknown>>({});
	const start = useRef<TourContext>(context);
	const shownStep = useRef(step);
	if (shownStep.current !== step) {
		shownStep.current = step;
		memory.current = {};
		start.current = context;
	}
	const done = current.done(context, memory.current, start.current);
	const ring = useRing(folded ? undefined : current.target, language);
	const last = step >= steps.length - 1;

	return (
		<>
			{ring && <div className="s2a-tour-ring" style={{ left: ring.left - 6, top: ring.top - 6, width: ring.width + 12, height: ring.height + 12 }} />}
			<Paper className="s2a-tour-card" shadow="lg" radius="md" p="md" withBorder role="dialog" aria-label={t("tour.title")}>
				<Stack gap="xs">
					<Group justify="space-between" wrap="nowrap" gap="xs">
						<Badge variant="light">{t("tour.progress", { step: String(step + 1), total: String(steps.length) })}</Badge>
						<Group gap={4} wrap="nowrap">
							<ActionIcon variant="subtle" aria-label={t(folded ? "tour.unfold" : "tour.fold")} onClick={() => setFolded(!folded)}>
								{folded ? <IconChevronUp size={16} /> : <IconChevronDown size={16} />}
							</ActionIcon>
							<ActionIcon variant="subtle" aria-label={t("tour.close")} onClick={onClose}>
								<IconX size={16} />
							</ActionIcon>
						</Group>
					</Group>
					<Progress value={((step + (done ? 1 : 0)) * 100) / steps.length} size="xs" />
					<Title order={5}>{current.title[language]}</Title>
					{!folded && (
						<>
							<Text size="sm">{current.text[language]}</Text>
							{current.action && (
								<Button variant="light" onClick={() => onAction(current.action!.run)} style={{ alignSelf: "flex-start" }}>
									{current.action.label[language]}
								</Button>
							)}
							{done && step > 0 && !last && (
								<Text size="sm" c="green" fw={500}>
									<IconCheck size={14} style={{ verticalAlign: "middle" }} /> {t("tour.step_done")}
								</Text>
							)}
						</>
					)}
					<Group justify="space-between">
						<Button variant="default" size="xs" disabled={step === 0} onClick={() => onStep(step - 1)}>
							{t("tour.back")}
						</Button>
						{last ? (
							<Button size="xs" onClick={onClose}>
								{t("tour.finish")}
							</Button>
						) : (
							<Button size="xs" disabled={!done} onClick={() => onStep(step + 1)}>
								{t("tour.next")}
							</Button>
						)}
					</Group>
				</Stack>
			</Paper>
		</>
	);
}
