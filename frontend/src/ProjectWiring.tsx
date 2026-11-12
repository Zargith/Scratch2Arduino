import { Alert, Badge, Button, Group, Loader, Modal, Paper, Stack, Table, Tabs, Text } from "@mantine/core";
import { IconAlertTriangle, IconDownload, IconListDetails, IconPhoto, IconSchema } from "@tabler/icons-react";
import { useEffect, useState } from "react";

import { bridge, hasHost, type WiringPlan } from "@/bridge";
import type { Language } from "@/catalog";
import type { Example } from "@/examples";
import type { MessageKey } from "@/i18n";
import type { Project } from "@/project/convert";
import { ZoomableImage } from "@/ZoomableImage";

interface ProjectWiringProps {
	opened: boolean;
	/** The example being edited, if any: its drawn diagram is shown too. */
	example: Example | null;
	language: Language;
	/** The project as it is now (read when the window opens). */
	project(): Project;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** Highlight these blocks in the editor (the window closes). */
	onShowBlocks(blockIds: string[]): void;
	onClose(): void;
}

type State = { status: "loading" } | { status: "ready"; plan: WiringPlan } | { status: "failed"; message: string };

/** What to wire for the project, computed from its blocks: components, pins and warnings. */
/** Blocks among the ids of a component (components found in typed code have "code-<line>" ids). */
export function blockIdsOnly(ids: string[]): string[] {
	return ids.filter((id) => !id.startsWith("code-"));
}

/** Lines of the code where a component was found ("7, 12"), "" when it comes from blocks only. */
export function codeLines(ids: string[]): string {
	const lines = ids.filter((id) => /^code-\d+$/.test(id)).map((id) => Number(id.slice(5)));
	return [...new Set(lines)].sort((a, b) => a - b).join(", ");
}

export function ProjectWiring({ opened, example, language, project, t, onShowBlocks, onClose }: ProjectWiringProps) {
	const [state, setState] = useState<State>({ status: "loading" });
	const [tab, setTab] = useState<string | null>("diagram");
	const [saved, setSaved] = useState<string | null>(null);

	useEffect(() => {
		if (!opened) return;
		// An example opens on its hand-drawn breadboard layout; other projects on the drawn diagram.
		setTab(example ? "example" : "diagram");
		setSaved(null);
		if (!hasHost()) {
			setState({ status: "failed", message: t("wiring.no_host") });
			return;
		}
		setState({ status: "loading" });
		let current = true;
		bridge
			.wiring(project(), language)
			.then((plan) => current && setState({ status: "ready", plan }))
			.catch((error: unknown) => current && setState({ status: "failed", message: String(error) }));
		return () => {
			current = false;
		};
		// The project is read once, when the window opens: the editor is behind it.
	}, [opened, language, example]);

	const svg = state.status === "ready" ? state.plan.svg : undefined;
	const save = async () => {
		if (!svg) return;
		try {
			const file = await bridge.saveImage(project().name, await svgToPng(svg), language);
			if (file) setSaved(t("wiring.saved", { file }));
		} catch (error) {
			setSaved(String(error));
		}
	};
	return (
		<Modal opened={opened} onClose={onClose} title={t("wiring.project_title")} size="90rem">
			<Tabs value={tab} onChange={setTab}>
				<Tabs.List mb="sm">
					{example && (
						<Tabs.Tab value="example" leftSection={<IconPhoto size={16} />}>
							{t("wiring.tab_example")}
						</Tabs.Tab>
					)}
					<Tabs.Tab value="diagram" leftSection={<IconSchema size={16} />}>
						{t("wiring.tab_diagram")}
					</Tabs.Tab>
					<Tabs.Tab value="components" leftSection={<IconListDetails size={16} />}>
						{t("wiring.tab_components")}
					</Tabs.Tab>
				</Tabs.List>
				<Tabs.Panel value="diagram">
					{svg ? (
						<Stack gap="xs">
							<ZoomableImage src={svgUrl(svg)} height="65vh" alt={t("wiring.project_title")} t={t} />
							<Group justify="space-between">
								<Text size="sm" c="dimmed">
									{t("wiring.diagram_help")}
								</Text>
								<Button size="xs" variant="light" leftSection={<IconDownload size={14} />} onClick={() => void save()}>
									{t("wiring.save_image")}
								</Button>
							</Group>
							{saved && (
								<Text size="sm" c="dimmed">
									{saved}
								</Text>
							)}
						</Stack>
					) : (
						<WiringTable state={state} t={t} onShowBlocks={onShowBlocks} />
					)}
				</Tabs.Panel>
				<Tabs.Panel value="components">
					<WiringTable state={state} t={t} onShowBlocks={onShowBlocks} />
				</Tabs.Panel>
				{example && (
					<Tabs.Panel value="example">
						<Stack gap="xs">
							<ZoomableImage
								src={example.wiring}
								height="65vh"
								alt={t("wiring.alt", { name: example.title[language] })}
								t={t}
							/>
							<Text size="sm" c="dimmed">
								{t("gallery.parts", { parts: example.parts[language] })}
							</Text>
						</Stack>
					</Tabs.Panel>
				)}
			</Tabs>
		</Modal>
	);
}

interface WiringTableProps {
	state: State;
	t(key: MessageKey, params?: Record<string, string>): string;
	onShowBlocks(blockIds: string[]): void;
}

function WiringTable({ state, t, onShowBlocks }: WiringTableProps) {
	if (state.status === "loading") {
		return (
			<Group justify="center" p="xl">
				<Loader size="sm" />
			</Group>
		);
	}
	if (state.status === "failed") {
		return (
			<Alert color="orange" variant="light">
				{state.message}
			</Alert>
		);
	}
	const { plan } = state;
	return (
		<Stack gap="sm">
			{plan.warnings.map((warning, index) => (
				<Alert key={index} color="orange" variant="light" icon={<IconAlertTriangle size={18} />}>
					<Group justify="space-between" wrap="nowrap" gap="sm">
						<Text size="sm">{warning.message}</Text>
						{blockIdsOnly(warning.blockIds).length > 0 && (
							<Button size="xs" variant="light" color="orange" onClick={() => onShowBlocks(blockIdsOnly(warning.blockIds))}>
								{t("wiring.show_blocks")}
							</Button>
						)}
					</Group>
				</Alert>
			))}
			{plan.components.length === 0 && <Text c="dimmed">{t("wiring.nothing")}</Text>}
			{plan.components.length > 0 && (
				<Text size="sm" c="dimmed">
					{t("wiring.intro")}
				</Text>
			)}
			{plan.components.map((component, index) => (
				<Paper key={`${component.kind}-${index}`} withBorder p="sm">
					<Group justify="space-between" mb="xs" wrap="nowrap">
						<Group gap="xs">
							<Badge variant="light" circle>
								{index + 1}
							</Badge>
							<Text fw={700}>{component.name}</Text>
						</Group>
						<Group gap="xs" wrap="nowrap">
							{codeLines(component.blockIds) && (
								<Text size="xs" c="dimmed">
									{t("wiring.code_lines", { lines: codeLines(component.blockIds) })}
								</Text>
							)}
							{blockIdsOnly(component.blockIds).length > 0 && (
								<Button size="xs" variant="subtle" onClick={() => onShowBlocks(blockIdsOnly(component.blockIds))}>
									{t("wiring.show_blocks")}
								</Button>
							)}
						</Group>
					</Group>
					<Table striped withTableBorder verticalSpacing={4}>
						<Table.Thead>
							<Table.Tr>
								<Table.Th>{t("wiring.component_pin")}</Table.Th>
								<Table.Th>{t("wiring.board_pin")}</Table.Th>
								<Table.Th>{t("wiring.via")}</Table.Th>
							</Table.Tr>
						</Table.Thead>
						<Table.Tbody>
							{component.connections.map((connection, row) => (
								<Table.Tr key={row}>
									<Table.Td>{connection.componentPin}</Table.Td>
									<Table.Td>
										<Text fw={700} size="sm">
											{connection.boardPin}
										</Text>
									</Table.Td>
									<Table.Td c={connection.via ? undefined : "dimmed"}>{connection.via || t("wiring.direct")}</Table.Td>
								</Table.Tr>
							))}
						</Table.Tbody>
					</Table>
					{component.note && (
						<Text size="sm" c="dimmed" mt={6}>
							{component.note}
						</Text>
					)}
				</Paper>
			))}
		</Stack>
	);
}

/** The SVG as an image URL. */
export function svgUrl(svg: string): string {
	return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
}

/** The SVG drawn twice as big on a canvas, as a PNG data URL. */
async function svgToPng(svg: string): Promise<string> {
	const image = new Image();
	await new Promise<void>((done, fail) => {
		image.onload = () => done();
		image.onerror = () => fail(new Error("SVG"));
		image.src = svgUrl(svg);
	});
	const canvas = document.createElement("canvas");
	canvas.width = image.naturalWidth * 2;
	canvas.height = image.naturalHeight * 2;
	const context = canvas.getContext("2d");
	if (!context) throw new Error("canvas");
	context.scale(2, 2);
	context.drawImage(image, 0, 0);
	return canvas.toDataURL("image/png");
}
