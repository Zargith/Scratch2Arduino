import { Button, Card, Image, Modal, SegmentedControl, SimpleGrid, Stack, Text, ThemeIcon } from "@mantine/core";
import { IconFilePlus } from "@tabler/icons-react";
import { useState } from "react";

import type { Language } from "@/catalog";
import { exampleGroups, type Example } from "@/examples";
import type { MessageKey } from "@/i18n";

interface GalleryProps {
	opened: boolean;
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	onBlank(): void;
	onExample(example: Example): void;
	onClose(): void;
}

const imageHeight = 150;

/** "New": a blank project, or a copy of an example with its wiring diagram. */
export function Gallery({ opened, language, t, onBlank, onExample, onClose }: GalleryProps) {
	return (
		<Modal opened={opened} onClose={onClose} title={t("gallery.title")} size="80rem">
			<SimpleGrid cols={{ base: 1, sm: 2, lg: 4 }} spacing="md">
				<Card withBorder padding="sm">
					<Card.Section>
						<Stack h={imageHeight} align="center" justify="center" bg="var(--mantine-color-gray-0)">
							<ThemeIcon size={64} radius="xl" variant="light">
								<IconFilePlus size={36} />
							</ThemeIcon>
						</Stack>
					</Card.Section>
					<Text fw={700} mt="sm">
						{t("gallery.blank")}
					</Text>
					<Text size="sm" c="dimmed" style={{ flex: 1 }}>
						{t("gallery.blank_description")}
					</Text>
					<Button mt="sm" onClick={onBlank} data-autofocus>
						{t("gallery.start")}
					</Button>
				</Card>
				{exampleGroups.map((variants) => (
					<ExampleCard key={variants[0]?.group} variants={variants} language={language} t={t} onExample={onExample} />
				))}
			</SimpleGrid>
		</Modal>
	);
}

interface ExampleCardProps {
	/** The example, or its variants (same program, other parts): the user picks one. */
	variants: Example[];
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	onExample(example: Example): void;
}

function ExampleCard({ variants, language, t, onExample }: ExampleCardProps) {
	const [chosen, setChosen] = useState(variants[0]?.id ?? "");
	const example = variants.find((candidate) => candidate.id === chosen) ?? variants[0];
	if (!example) return null;
	return (
		<Card withBorder padding="sm">
			<Card.Section>
				<Image src={example.wiring} h={imageHeight} fit="contain" alt={t("wiring.alt", { name: example.title[language] })} />
			</Card.Section>
			<Text fw={700} mt="sm">
				{example.title[language]}
			</Text>
			<Text size="sm" style={{ flex: 1 }}>
				{example.description[language]}
			</Text>
			{variants.length > 1 && (
				<SegmentedControl
					mt="xs"
					size="xs"
					fullWidth
					value={example.id}
					onChange={setChosen}
					aria-label={t("gallery.variant")}
					data={variants.map((variant) => ({ value: variant.id, label: variant.variant?.[language] ?? variant.id }))}
				/>
			)}
			<Text size="xs" c="dimmed" mt={4}>
				{t("gallery.parts", { parts: example.parts[language] })}
			</Text>
			<Button mt="sm" variant="light" onClick={() => onExample(example)}>
				{t("gallery.start")}
			</Button>
		</Card>
	);
}

interface WiringProps {
	example: Example | null;
	opened: boolean;
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	onClose(): void;
}

/** The wiring diagram of the example being edited. */
export function Wiring({ example, opened, language, t, onClose }: WiringProps) {
	return (
		<Modal
			opened={opened && example !== null}
			onClose={onClose}
			title={
				example
					? t("wiring.title", {
							name: example.variant ? `${example.title[language]} (${example.variant[language]})` : example.title[language],
						})
					: ""
			}
			size="70rem"
		>
			{example && (
				<Stack gap="xs">
					<Image src={example.wiring} fit="contain" mah="75vh" alt={t("wiring.alt", { name: example.title[language] })} />
					<Text size="sm" c="dimmed">
						{t("gallery.parts", { parts: example.parts[language] })}
					</Text>
				</Stack>
			)}
		</Modal>
	);
}
