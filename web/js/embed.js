// Compact mode: hide the top navigation when the page runs inside an iframe
// (e.g. a Home Assistant webpage card) or when called with ?embed.
// ?embed=0 forces the navigation back on.
(function () {
  var embedded;
  try {
    embedded = window.self !== window.top;
  } catch (e) {
    embedded = true;
  }
  if (/[?&]embed(=1)?(&|$)/.test(location.search)) embedded = true;
  if (/[?&]embed=0(&|$)/.test(location.search)) embedded = false;
  if (embedded) document.documentElement.className += " embedded";
})();
