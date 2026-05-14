// MathJax configuration for Doxygen documentation
window.MathJax =
{
  tex:
  {
    // Enable $...$ for inline math (in addition to \(...\))
    inlineMath: [['$', '$'], ['\\(', '\\)']],
    displayMath: [['$$', '$$'], ['\\[', '\\]']],
    processEscapes: true,      // Allow \$ to produce literal $
    processEnvironments: true  // Process \begin{...}...\end{...}
  },
  options:
  {
    ignoreHtmlClass: 'tex2jax_ignore',
    processHtmlClass: 'tex2jax_process'
  },
  output:
  {
    font: 'mathjax-fira'
  },
  chtml:
  {
    displayAlign: 'left',
    displayIndent: '2em'
  },
  svg:
  {
    displayAlign: 'left',
    displayIndent: '2em'
  }
};
