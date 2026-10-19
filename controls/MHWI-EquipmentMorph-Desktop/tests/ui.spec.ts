import { test, expect } from "@playwright/test";
test.beforeEach(async ({page}) => { await page.goto("/?fixture=connected"); await expect(page.getByLabel("本窗口权重")).toBeEnabled(); });
test("preserves typed precision through polling and applies float32", async ({page}) => {
  const edit=page.getByLabel("本窗口权重");await edit.fill("0.24681357");
  await expect(edit).toHaveValue("0.24681357");await page.waitForTimeout(600);await expect(edit).toHaveValue("0.24681357");
  await edit.press("Enter");await expect(page.locator("tbody tr").first().locator("td").nth(1)).toHaveText(Number(Math.fround(.24681357).toPrecision(9)).toString());
});
test("zero takes ownership and drop releases only selected key",async({page})=>{
  await page.getByRole("button",{name:"smile",exact:true}).click();
  await page.getByLabel("本窗口权重").fill("0");await page.getByRole("button",{name:"应用权重",exact:true}).click();
  await expect(page.getByLabel("由本窗口接管")).toBeChecked();await expect(page.getByRole("button",{name:"释放全部控制（2）"})).toBeEnabled();
  await page.getByLabel("由本窗口接管").uncheck();await expect(page.getByRole("button",{name:"释放全部控制（1）"})).toBeEnabled();
});
test("invalid weight never replaces current contribution",async({page})=>{
  await page.getByLabel("本窗口权重").fill("NaN");await page.getByRole("button",{name:"应用权重",exact:true}).click();
  await expect(page.getByRole("alert")).toContainText("请输入");
  await expect(page.locator("tbody tr").first().locator("td").nth(1)).toHaveText("0.137246817");
});
test("release all clears ownership",async({page})=>{
  await page.getByRole("button",{name:"释放全部控制（1）"}).click();
  await expect(page.getByLabel("由本窗口接管")).not.toBeChecked();await expect(page.getByRole("button",{name:"释放全部控制",exact:true})).toBeDisabled();
});
test("equipment selection uses full 64 bit string identity and never replays",async({page})=>{
  await expect(page.getByLabel("装备部位与模型")).toHaveValue("18446744073709551613");
  await page.getByLabel("装备部位与模型").selectOption("22");await expect(page.getByLabel("由本窗口接管")).not.toBeChecked();
  await expect(page.getByRole("button",{name:"adjust",exact:true})).toBeVisible();await expect(page.getByLabel("本窗口权重")).toHaveValue("0");
});
test("search does not release existing controls",async({page})=>{
  await page.getByRole("searchbox").fill("not-present");await expect(page.getByText("没有匹配的形态键")).toBeVisible();
  await expect(page.getByRole("button",{name:"释放全部控制（1）"})).toBeEnabled();
});
test("disconnected state disables actions",async({page})=>{
  await page.goto("/?fixture=disconnected");await expect(page.getByLabel("本窗口权重")).toBeDisabled();
  await expect(page.getByRole("button",{name:"应用权重",exact:true})).toBeDisabled();await expect(page.getByRole("button",{name:"释放全部控制",exact:true})).toBeDisabled();
});
test("conflict is explicit and original value remains",async({page})=>{
  await page.goto("/?fixture=conflict");await expect(page.getByRole("alert")).toContainText("原贡献保留");
  await expect(page.getByLabel("由本窗口接管")).toBeChecked();
});
test("64 keys remain available without page overflow",async({page})=>{
  await page.goto("/?fixture=many");await expect(page.locator("tbody tr")).toHaveCount(64);
  await page.getByRole("button",{name:"morph_64",exact:true}).scrollIntoViewIfNeeded();await page.getByRole("button",{name:"morph_64",exact:true}).click();
  await expect(page.locator(".selected-name")).toHaveText("morph_64");
});
test("narrow layout keeps editing controls and document within viewport",async({page})=>{
  await page.setViewportSize({width:390,height:844});
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBeTruthy();
  const bounds=await page.getByRole("button",{name:"应用权重",exact:true}).boundingBox();
  expect(bounds && bounds.x>=0 && bounds.x+bounds.width<=390).toBeTruthy();
});
test("weight columns align numerically",async({page})=>{
  await expect(page.locator("tbody tr").first().locator("td").nth(1)).toHaveCSS("text-align","right");
  await expect(page.locator("tbody tr").first().locator("td").nth(2)).toHaveCSS("font-variant-numeric","tabular-nums");
});
