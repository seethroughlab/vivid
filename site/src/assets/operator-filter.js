document.addEventListener("DOMContentLoaded", () => {
  const root = document.querySelector(".operator-filter-root");
  if (!root) return;

  const panel = document.getElementById("operator-filter-panel");
  const searchInput = document.getElementById("operator-search");
  const domainSelect = document.getElementById("operator-domain");
  const clearButton = document.getElementById("operator-clear");
  const results = document.getElementById("operator-results");
  const empty = document.getElementById("operator-empty");
  const sections = Array.from(document.querySelectorAll(".operator-section"));
  const cards = Array.from(document.querySelectorAll(".operator-card"));

  if (!searchInput || !domainSelect || !clearButton || !results || !empty || cards.length === 0) {
    return;
  }

  if (panel) {
    panel.hidden = false;
  }

  const total = cards.length;

  function update() {
    const search = searchInput.value.trim().toLowerCase();
    const domain = domainSelect.value;
    let visibleCount = 0;

    for (const card of cards) {
      const matchesSearch = !search || (card.dataset.search || "").includes(search);
      const matchesDomain = !domain || card.dataset.domain === domain;
      const visible = matchesSearch && matchesDomain;
      card.hidden = !visible;
      if (visible) visibleCount += 1;
    }

    for (const section of sections) {
      const sectionCards = Array.from(section.querySelectorAll(".operator-card"));
      const anyVisible = sectionCards.some((card) => !card.hidden);
      section.hidden = !anyVisible;
    }

    empty.hidden = visibleCount !== 0;

    if (!search && !domain) {
      results.textContent = `Showing all ${total} operators.`;
      return;
    }

    const parts = [`Showing ${visibleCount} of ${total} operators.`];
    if (search) parts.push(`Search: "${searchInput.value.trim()}".`);
    if (domain) parts.push(`Domain: ${domain.toUpperCase()}.`);
    results.textContent = parts.join(" ");
  }

  searchInput.addEventListener("input", update);
  domainSelect.addEventListener("change", update);
  clearButton.addEventListener("click", () => {
    searchInput.value = "";
    domainSelect.value = "";
    update();
    searchInput.focus();
  });

  update();
});
