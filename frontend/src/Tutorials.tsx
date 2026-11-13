// Side panel showing the bundled user documentation (tutorials and guide), to follow a tutorial
// while building its blocks. Links between pages open in the panel; a tutorial can open its
// example from the gallery.
import { ActionIcon, Alert, Button, Card, Group, Text, Tooltip, Typography } from "@mantine/core";
import { IconExternalLink, IconHandClick, IconList, IconX } from "@tabler/icons-react";
import { type MouseEvent, useEffect, useMemo, useRef } from "react";

import type { Language } from "@/catalog";
import { type DocPath, docsHome, headingAnchor, renderDoc, resolveLink, tutorials } from "@/docs";
import type { MessageKey } from "@/i18n";

import "./Tutorials.css";

interface TutorialsProps {
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** The page shown, in docs/<language>. */
	page: DocPath;
	/** CSS height of the panel. */
	height: string;
	onNavigate(page: DocPath, language: Language): void;
	/** Opens the gallery example of a tutorial. */
	onOpenExample(exampleId: string): void;
	/** Starts the interactive tutorial (on a new project). */
	onStartTour?(): void;
	onClose(): void;
}

export function Tutorials({ language, t, page, height, onNavigate, onOpenExample, onStartTour, onClose }: TutorialsProps) {
	const html = useMemo(() => renderDoc(language, page), [language, page]);
	const content = useRef<HTMLDivElement>(null);
	const tutorial = tutorials.find((candidate) => candidate.path === page);
	/** Heading to scroll to once the page is shown (a link "page.md#anchor"). */
	const pendingAnchor = useRef("");

	const scrollTo = (anchor: string) => {
		const heading = [...(content.current?.querySelectorAll("h1, h2, h3, h4") ?? [])].find(
			(candidate) => headingAnchor(candidate.textContent ?? "") === anchor
		);
		heading?.scrollIntoView({ block: "start" });
	};

	useEffect(() => {
		if (!content.current) return;
		content.current.scrollTop = 0;
		if (pendingAnchor.current) scrollTo(pendingAnchor.current);
		pendingAnchor.current = "";
	}, [html]);

	const followLink = (event: MouseEvent<HTMLDivElement>) => {
		const link = (event.target as HTMLElement).closest("a");
		if (!link) return;
		event.preventDefault(); // never leave the editor
		const target = resolveLink(language, page, link.getAttribute("href") ?? "");
		if (target.kind === "anchor") scrollTo(target.anchor);
		else if (target.kind === "page") {
			pendingAnchor.current = target.anchor;
			onNavigate(target.path, target.language);
		}
	};

	return (
		<Card withBorder shadow="sm" w={440} padding="sm" style={{ height, display: "flex", flexDirection: "column" }}>
			<Group justify="space-between" wrap="nowrap" mb="xs">
				<Group gap="xs" wrap="nowrap">
					<Tooltip label={t("tutorials.home")}>
						<ActionIcon
							variant="default"
							aria-label={t("tutorials.home")}
							disabled={page === docsHome}
							onClick={() => onNavigate(docsHome, language)}
						>
							<IconList size={16} />
						</ActionIcon>
					</Tooltip>
					<Text fw={700} size="sm">
						{t("tutorials.title")}
					</Text>
				</Group>
				<Group gap="xs" wrap="nowrap">
					{tutorial && (
						<Button
							size="compact-sm"
							variant="light"
							leftSection={<IconExternalLink size={14} />}
							onClick={() => onOpenExample(tutorial.exampleId)}
						>
							{t("tutorials.open_example")}
						</Button>
					)}
					<ActionIcon variant="subtle" color="gray" aria-label={t("tutorials.close")} onClick={onClose}>
						<IconX size={16} />
					</ActionIcon>
				</Group>
			</Group>
			{page === docsHome && onStartTour && (
				<Alert variant="light" color="orange" mb="xs" title={t("tour.offer_title")} icon={<IconHandClick size={18} />}>
					<Text size="sm" mb="xs">
						{t("tour.offer")}
					</Text>
					<Button size="xs" onClick={onStartTour}>
						{t("tour.start")}
					</Button>
				</Alert>
			)}
			<Typography
				ref={content}
				onClick={followLink}
				className="s2a-doc"
				style={{ flex: 1, overflowY: "auto", fontSize: "var(--mantine-font-size-sm)", paddingRight: 4 }}
				// The documentation is bundled with the editor (docs/<language>), never typed by the user.
				dangerouslySetInnerHTML={{ __html: html }}
			/>
		</Card>
	);
}
