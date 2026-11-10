from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional

from ..errors import NotFoundError


@dataclass(frozen=True)
class FunctionSpec:
    id: str
    group: str
    title: str
    instruction: str
    output: str = "markdown"
    max_tokens: int = 700
    temperature: float = 0.3
    schema_hint: str = ""
    params: Dict[str, str] = field(default_factory=dict)
    verify_quotes: bool = False

    def to_dict(self) -> dict:
        return {
            "id": self.id,
            "group": self.group,
            "title": self.title,
            "output": self.output,
            "max_tokens": self.max_tokens,
            "params": dict(self.params),
        }


class FunctionCatalog:
    def __init__(self, specs: Iterable[FunctionSpec]) -> None:
        self._specs: Dict[str, FunctionSpec] = {}
        for spec in specs:
            if spec.id in self._specs:
                raise ValueError(f"duplicate function id: {spec.id}")
            self._specs[spec.id] = spec

    def get(self, function_id: str) -> FunctionSpec:
        spec = self._specs.get(function_id)
        if spec is None:
            raise NotFoundError(f"unknown research function: {function_id}")
        return spec

    def has(self, function_id: str) -> bool:
        return function_id in self._specs

    def all(self) -> List[FunctionSpec]:
        return list(self._specs.values())

    def by_group(self, group: str) -> List[FunctionSpec]:
        return [spec for spec in self._specs.values() if spec.group == group]

    def groups(self) -> Dict[str, int]:
        counts: Dict[str, int] = {}
        for spec in self._specs.values():
            counts[spec.group] = counts.get(spec.group, 0) + 1
        return counts

    def __len__(self) -> int:
        return len(self._specs)


def _spec(identifier: str, group: str, title: str, instruction: str, output: str = "markdown", *,
          tokens: int = 700, temperature: float = 0.3, schema: str = "", params: Optional[Dict[str, str]] = None,
          verify: bool = False) -> FunctionSpec:
    return FunctionSpec(identifier, group, title, instruction, output, tokens, temperature, schema,
                        params or {}, verify)


_SPECS: List[FunctionSpec] = [
    _spec("summary.plain", "understand", "Plain-language summary",
          "Summarize the passage in plain language for a general reader in about ${words} words. Keep every claim faithful to the passage.",
          tokens=500, params={"words": "80"}),
    _spec("summary.tweet", "understand", "Tweet-length summary",
          "Summarize the passage in a single sentence of at most 240 characters.", "text", tokens=120),
    _spec("summary.paragraph", "understand", "Paragraph summary",
          "Write one well-structured paragraph that captures the main point, the support offered for it, and the conclusion.",
          tokens=400),
    _spec("summary.deep", "understand", "Deep summary",
          "Write a detailed multi-paragraph summary covering the claims, the reasoning, notable terms, and what the passage leaves unsaid.",
          tokens=1200),
    _spec("keypoints", "understand", "Key points",
          "Extract the ${count} most important points as a bullet list. Each bullet is one short sentence.", "list",
          tokens=500, params={"count": "5"}),
    _spec("eli15", "understand", "Explain like I'm 15",
          "Explain the passage to a curious fifteen-year-old using everyday words and one concrete example.", tokens=600),
    _spec("academic", "understand", "Academic rephrasing",
          "Rephrase the passage in formal academic prose without changing its meaning, preserving technical terms.",
          tokens=600),
    _spec("contrastive", "understand", "Claims versus omissions",
          "Produce two sections. 'Claims' lists what the passage asserts. 'Omissions' lists relevant questions or considerations it does not address.",
          tokens=700),
    _spec("historical_frame", "understand", "Historical and contextual framing",
          "Explain the historical and cultural setting in which this passage was written or should be read. Separate what is stated in the passage from what you add as background.",
          tokens=800),
    _spec("glossary", "understand", "Terminology glossary",
          "List the technical, archaic, or ambiguous terms in the passage with short definitions suited to the passage's usage.",
          "json", tokens=700, schema='[{"term": "string", "definition": "string"}]'),
    _spec("concept_map", "understand", "Concept relationship diagram",
          "Draw the main concepts in the passage and the relationships between them as a Mermaid flowchart with at most 12 nodes.",
          "mermaid", tokens=700, temperature=0.2),
    _spec("reading_level", "understand", "Reading level and difficulty",
          "Assess the reading difficulty of the passage for a general adult reader.",
          "json", tokens=300, temperature=0.0,
          schema='{"grade_level": number, "difficulty": "easy|moderate|hard|very_hard", "reasons": ["string"]}'),
    _spec("study_questions", "understand", "Study questions",
          "Write ${count} study questions of increasing difficulty that test comprehension, analysis, and application of the passage.",
          "json", tokens=700, params={"count": "6"},
          schema='[{"question": "string", "level": "recall|analysis|application", "answer_hint": "string"}]'),
    _spec("significance", "understand", "Why this matters",
          "State in two to four sentences why this passage matters, to whom, and what would change if it were misunderstood.",
          tokens=400),
    _spec("argument_structure", "understand", "Argument structure",
          "Extract the argument: the conclusion, the premises that support it, and any implicit steps.",
          "json", tokens=800,
          schema='{"conclusion": "string", "premises": ["string"], "implicit_steps": ["string"], "type": "deductive|inductive|abductive|narrative|other"}'),
    _spec("assumptions", "understand", "Assumption detector",
          "List the unstated assumptions the passage relies on and say how each one could be tested or challenged.",
          "list", tokens=600),
    _spec("implications", "understand", "Implications and consequences",
          "Describe the logical, practical, and ethical implications that follow if the passage is accepted as true.",
          tokens=700),

    _spec("quotes.key", "evidence", "Exact quote extractor",
          "Select up to ${count} sentences or phrases from the passage that best capture its meaning. Copy each quote exactly as it appears, character for character.",
          "json", tokens=600, temperature=0.0, params={"count": "3"},
          schema='[{"quote": "string", "why": "string"}]', verify=True),
    _spec("paraphrase.side_by_side", "evidence", "Original versus paraphrase",
          "Split the passage into its natural sentences and give a faithful paraphrase for each.",
          "json", tokens=1200, schema='[{"original": "string", "paraphrase": "string"}]', verify=False),
    _spec("evidence.strength", "evidence", "Evidence strength rating",
          "Rate the strength of the evidence the passage offers for its main claim and explain the rating by referring to the text.",
          "json", tokens=500, temperature=0.1,
          schema='{"rating": "none|weak|moderate|strong", "score": number, "justification": "string", "missing_evidence": ["string"]}'),
    _spec("crossref.document", "evidence", "Cross-reference within the document",
          "Using the other passages listed in <related>, identify which ones this passage connects to and explain each connection in one sentence.",
          "json", tokens=700, schema='[{"pack_id": "string", "relation": "supports|contradicts|elaborates|repeats|contrasts", "note": "string"}]'),
    _spec("author_intent", "evidence", "Author intent inference",
          "Infer the likely purpose and intended audience of the passage. Label each inference as strongly or weakly supported by the text.",
          tokens=600),
    _spec("rhetoric", "evidence", "Rhetorical devices",
          "Identify rhetorical devices in the passage, quote the words that show each one, and explain its effect.",
          "json", tokens=800, schema='[{"device": "string", "quote": "string", "effect": "string"}]', verify=False),
    _spec("crossref.domain", "evidence", "Domain cross-references",
          "Name concepts, fields, or works in other disciplines that this passage connects to, with one sentence on each connection.",
          "list", tokens=600),
    _spec("manuscript_links", "evidence", "Manuscript and textual tradition notes",
          "If this passage has a known manuscript or textual tradition, describe the main witnesses and notable variants. If you do not know of any, say so plainly instead of guessing.",
          tokens=700),
    _spec("primary_sources", "evidence", "Primary source locator",
          "Suggest what kinds of primary sources and archives would let a reader verify this passage, and name specific ones only when you are confident they exist.",
          "list", tokens=600),

    _spec("expand.background", "expand", "Background context",
          "Provide the background a newcomer needs to understand this passage: key facts, prior debates, and relevant definitions.",
          tokens=900),
    _spec("expand.timeline", "expand", "Historical timeline",
          "Build a chronological timeline of events directly relevant to the passage. Use dates only when you are confident of them.",
          "json", tokens=900, schema='[{"when": "string", "event": "string", "relevance": "string"}]'),
    _spec("expand.entities", "expand", "People, places, and events",
          "List the key people, places, and events the passage mentions or depends on with one line of context each.",
          "json", tokens=900, schema='[{"name": "string", "kind": "person|place|event|group|work", "context": "string"}]'),
    _spec("expand.related_concepts", "expand", "Related concepts",
          "List concepts in the passage that connect to one another and explain each link briefly.",
          "list", tokens=600),
    _spec("expand.further_reading", "expand", "Further reading",
          "Recommend ${count} books or long-form works for deeper study. Include only works you are confident exist, and say why each helps.",
          "json", tokens=700, params={"count": "5"},
          schema='[{"title": "string", "author": "string", "why": "string"}]'),
    _spec("expand.papers", "expand", "Academic paper suggestions",
          "Suggest research topics and search phrases a reader could use to find academic papers about this passage. Do not invent specific paper titles.",
          "json", tokens=600, schema='{"topics": ["string"], "search_phrases": ["string"], "fields": ["string"]}'),
    _spec("expand.news", "expand", "News and current-event angles",
          "Describe current-event themes that this passage connects to, and give search phrases a reader could use to find recent coverage. Do not state specific news facts you cannot verify.",
          "json", tokens=500, schema='{"themes": ["string"], "search_phrases": ["string"]}'),
    _spec("expand.videos", "expand", "Video recommendations",
          "Suggest the kinds of lectures, documentaries, or talks that would deepen understanding of this passage and give search phrases to find them.",
          "json", tokens=500, schema='{"kinds": ["string"], "search_phrases": ["string"]}'),
    _spec("expand.podcasts", "expand", "Podcast and audio suggestions",
          "Suggest topics for podcasts or audio lectures related to the passage and give search phrases to find them.",
          "json", tokens=500, schema='{"topics": ["string"], "search_phrases": ["string"]}'),
    _spec("expand.datasets", "expand", "Dataset suggestions",
          "Describe datasets or statistical collections a researcher could use to test claims related to this passage, naming only well-known sources.",
          "list", tokens=500),
    _spec("expand.opposing", "expand", "Opposing and alternative viewpoints",
          "Present the strongest serious alternative viewpoints to the passage, each stated fairly in its own best terms.",
          tokens=900),
    _spec("expand.geography", "expand", "Geographic and map context",
          "Describe the geographic setting relevant to the passage and list the places a reader should locate on a map.",
          "json", tokens=700, schema='[{"place": "string", "region": "string", "significance": "string"}]'),
    _spec("expand.idea_timeline", "expand", "Idea development timeline",
          "Trace how the central idea of the passage developed over time, naming thinkers or traditions only when you are confident.",
          "json", tokens=900, schema='[{"period": "string", "development": "string"}]'),
    _spec("expand.mini_essay", "expand", "Deep-dive mini essay",
          "Write a focused essay of about ${words} words that expands on the passage's central idea with structure, evidence, and a conclusion.",
          tokens=1600, temperature=0.5, params={"words": "500"}),
    _spec("expand.comparative", "expand", "Comparative philosophy and theology",
          "Compare the passage's central idea with parallel ideas in other philosophical or religious traditions, noting similarities and differences fairly.",
          tokens=1000),
    _spec("expand.science", "expand", "Scientific research angles",
          "Describe scientific fields that bear on the passage and the kinds of findings that would support or undermine its claims. Do not fabricate study details.",
          tokens=800),
    _spec("expand.statistics", "expand", "Statistical source finder",
          "Name the organizations or databases that publish reliable statistics relevant to the passage and the specific measures to look for.",
          "list", tokens=500),
    _spec("expand.culture", "expand", "Cultural context notes",
          "Explain cultural customs, idioms, or conventions that a modern reader might miss in this passage.",
          tokens=700),
    _spec("expand.futures", "expand", "Future implication scenarios",
          "Outline three plausible future scenarios that follow from taking the passage seriously: an optimistic one, a pessimistic one, and a mixed one.",
          "json", tokens=800, temperature=0.6, schema='[{"scenario": "optimistic|pessimistic|mixed", "description": "string", "assumptions": ["string"]}]'),

    _spec("assist.critique", "assist", "Critique",
          "Give a rigorous but fair critique of the passage, covering strengths first and then weaknesses in reasoning, evidence, and clarity.",
          tokens=900),
    _spec("assist.counter", "assist", "Counter-argue",
          "Argue against the passage's main claim as strongly and fairly as possible, then note where the counter-argument is weakest.",
          tokens=900),
    _spec("assist.teach", "assist", "Teach me",
          "Teach the passage's content as a short lesson with a learning objective, an explanation, a worked example, and two check-yourself questions.",
          tokens=1000),
    _spec("assist.analogy", "assist", "Analogy generator",
          "Give three analogies that illuminate the passage's central idea, each from a different everyday domain, and say where each analogy breaks down.",
          tokens=600, temperature=0.7),
    _spec("assist.metaphor", "assist", "Metaphor generator",
          "Offer three fresh metaphors for the passage's central idea and explain what each highlights.",
          tokens=500, temperature=0.8),
    _spec("assist.safety_bias", "assist", "Safety and bias notes",
          "Note any bias, loaded language, one-sidedness, or sensitive content in the passage that a reader should be aware of, quoting the relevant words.",
          "json", tokens=600, schema='[{"issue": "string", "quote": "string", "note": "string"}]'),
    _spec("assist.tutor", "assist", "Interactive tutor step",
          "Act as a Socratic tutor. Ask one probing question about the passage that helps the reader reason toward understanding, and offer a hint only if asked.",
          tokens=300, temperature=0.5),
    _spec("assist.translate", "assist", "Translation",
          "Translate the passage into ${language}, preserving tone and meaning, and add brief notes for any phrase that does not translate directly.",
          tokens=1200, params={"language": "Spanish"}),

    _spec("visual.summary_prompt", "visual", "Summary image prompt",
          "Write one image-generation prompt of at most 80 words that visually expresses the meaning of the passage. Describe subject, composition, lighting, and mood. Do not include any text or lettering in the image.",
          "text", tokens=250, temperature=0.7),
    _spec("visual.diagram", "visual", "Concept diagram",
          "Create an infographic-style concept diagram of the passage as a Mermaid diagram with clear labels.",
          "mermaid", tokens=800, temperature=0.2),
    _spec("visual.timeline", "visual", "Timeline visualization data",
          "Produce data for a timeline visualization of the sequence of events or ideas in the passage.",
          "json", tokens=700, temperature=0.1, schema='[{"label": "string", "order": number, "detail": "string"}]'),
    _spec("visual.symbolic_prompt", "visual", "Symbolic illustration prompt",
          "Write an image-generation prompt of at most 80 words for a symbolic, illuminated-manuscript style illustration of the passage's central idea. No text or lettering.",
          "text", tokens=250, temperature=0.8),
    _spec("visual.contrast_prompt", "visual", "Contrast visual prompt",
          "Write an image-generation prompt of at most 80 words for a split image contrasting the two main ideas or states in the passage. No text or lettering.",
          "text", tokens=250, temperature=0.7),
    _spec("visual.set_plan", "visual", "Image set plan",
          "Break the passage into up to ${count} visual scenes or ideas and write an image-generation prompt of at most 60 words for each. No text or lettering in any image.",
          "json", tokens=1200, temperature=0.7, params={"count": "4"},
          schema='[{"scene": "string", "prompt": "string"}]'),

    _spec("output.study_guide", "output", "Study guide",
          "Write a study guide for this passage with these sections: Overview, Key Terms, Main Ideas, Discussion Questions, and Review Checklist.",
          tokens=1800),
    _spec("output.flashcards", "output", "Flashcards",
          "Create ${count} flashcards that test the passage's key facts, terms, and ideas. Each card has a short front and a concise back.",
          "json", tokens=1000, temperature=0.2, params={"count": "8"}, schema='[{"front": "string", "back": "string"}]'),
    _spec("output.slides", "output", "Presentation slide outline",
          "Create an outline for a ${count}-slide presentation about the passage. Each slide has a title, up to four bullets, and optional speaker notes.",
          "json", tokens=1400, params={"count": "6"},
          schema='[{"title": "string", "bullets": ["string"], "notes": "string"}]'),
]

CATALOG = FunctionCatalog(_SPECS)
