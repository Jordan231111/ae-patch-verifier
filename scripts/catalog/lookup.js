const { resolveLatestXapk } = require("../../api/_shared/apkpure");

resolveLatestXapk(process.argv[2]).then(({ versionCode, versionName }) => {
  console.log(JSON.stringify({ versionCode, versionName }));
}).catch(error => {
  console.error(error.message);
  process.exitCode = 1;
});
