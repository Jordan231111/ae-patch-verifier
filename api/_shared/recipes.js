const inputs = require("../../scripts/catalog/inputs.json");

function recipe(app) {
  const shared = [inputs.anotherEdenPatcher.sha256, inputs.sharedSigning.certificateSha256];
  if (app === "anothereden") return [...shared, inputs.aeLogin.companion.sha256,
    inputs.aeLogin.preparation.sha256, `prepare-v${inputs.aeLogin.preparation.schema}`].join("/");
  if (inputs[app]?.packager) shared.push(inputs[app].packager.commit);
  if (app === "epicconquest") shared.push(inputs.aeLogin.companion.sha256);
  return shared.join("/");
}

module.exports = { recipe };
