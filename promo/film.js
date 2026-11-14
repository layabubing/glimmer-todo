"use strict";

const byId = id => document.getElementById(id);
const player = byId("player");
const stage = byId("stage");
const canvas = byId("art");
const ctx = canvas.getContext("2d");
const audio = byId("score");
const seek = byId("seek");
const play = byId("play");
const chapters = [...document.querySelectorAll("#chapters button")];
const shots = [...document.querySelectorAll(".shot")];
const duration = 36;
const paper = "#efefeb";
const blue = "#3455ee";
const assets = Object.fromEntries(["tasks","completed","schedule","running-0","running-1","running-2","running-3","capsule","capsule-light","capsule-forest","topbar","taskbar"].map(name => {
  const image = new Image();
  image.src = "assets/" + name + ".png";
  return [name, image];
}));
let animation = 0;
let position = 0;
let controlsTimer;
let chapterIndex = -1;
let activeShot;

const clamp = value => Math.max(0, Math.min(1, value));
const ease = value => 1 - Math.pow(1 - clamp(value), 3);
const smooth = value => { const x = clamp(value); return x*x*(3-2*x); };
const timecode = seconds => "00:" + String(Math.floor(seconds)).padStart(2, "0");

function background(color) {
  ctx.fillStyle = color;
  ctx.fillRect(0, 0, 1600, 900);
}

function mark(x, y, scale, color) {
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(scale, scale);
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(-249, -8);
  ctx.lineTo(-167, -88);
  ctx.lineTo(-54, 25);
  ctx.lineTo(206, -240);
  ctx.lineTo(289, -158);
  ctx.lineTo(-54, 191);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
}

function introArt(t) {
  const spacing = 1 - ease((t-1.5)/1.2);
  const zoom = 1 + 5.2*smooth((t-3)/1);
  ctx.save();
  ctx.translate(1220,448);
  ctx.scale(zoom,zoom);
  ctx.translate(-1220,-448);
  const marks = [
    [-195, 80, "#d5dcf4"], [-130, 53, "#b2bef5"],
    [-65, 26, "#8498f7"], [0, 0, blue]
  ];
  for (const [dx,dy,color] of marks) {
    mark(1240 + dx*spacing, 434 + dy*spacing, 1.28, color);
  }
  ctx.restore();
}

function showShot(name) {
  if (activeShot === name) return;
  activeShot = name;
  for (const shot of shots) shot.hidden = shot.id !== name;
}

function setImage(element, name) {
  const image = assets[name];
  if (element.src !== image.src) element.src = image.src;
}

function showWork(t) {
  const schedule = t >= 6.5;
  const local = t - (schedule ? 6.5 : 4);
  background(schedule ? "#e5eafa" : "#f7f7f3");
  ctx.fillStyle = schedule ? "#ced9fa" : "#e8eafa";
  ctx.beginPath();
  ctx.arc(1550, 890, 705 + 25*ease(local/3), 0, 2*Math.PI);
  ctx.fill();
  byId("work-title").textContent = schedule ? "到点提醒。" : "记下，勾选。";
  byId("work-caption").textContent = schedule ? "日程支持每天或每周重复。" : "回车添加待办，勾选即可完成。";
  setImage(byId("work-image"), schedule ? "schedule" : t < 5.5 ? "tasks" : "completed");
  const window = byId("native");
  const enter = ease(local / 0.55);
  const inspect = schedule ? 0 : smooth((t-4.75)/0.5)*(1-smooth((t-6)/0.5));
  window.style.transform = "translate(" + (1-enter)*22 + "%,-50%) scale(" + (0.96+0.04*enter+inspect*0.19) + ")";
  window.style.opacity = enter;
  byId("work").querySelector(".shot-copy").style.opacity = clamp(local/0.16);
  const ring = byId("tap");
  const ripple = clamp((t-5.5)/0.5);
  ring.style.opacity = t >= 5.5 && t < 6 ? 1-ripple : 0;
  ring.style.transform = "translate(-50%,-50%) scale(" + (0.35+ripple) + ")";
}

function showFocus(t) {
  background("#171a23");
  const enter = ease((t-8)/0.45);
  const reveal = enter;
  ctx.save();
  ctx.strokeStyle = "#34394b";
  ctx.lineWidth = 2;
  for (let i=0; i<60; i++) {
    const angle = (i-15)*Math.PI/30;
    const x = Math.cos(angle), y = Math.sin(angle);
    ctx.strokeStyle = i < Math.floor((t-8)*2) ? "#9aaaff" : "#34394b";
    ctx.beginPath();
    ctx.moveTo(1165+x*371,448+y*371);
    ctx.lineTo(1165+x*(i%5===0?340:356),448+y*(i%5===0?340:356));
    ctx.stroke();
  }
  ctx.restore();
  byId("focus-type").style.transform = "translate(" + (1-enter)*12 + "%,-50%)";
  const image = document.querySelector(".native-focus");
  const elapsed = Math.floor(t-8);
  const remaining = 1500-elapsed;
  byId("minutes").textContent = Math.floor(remaining/60);
  byId("seconds").textContent = String(remaining%60).padStart(2,"0");
  setImage(image, "running-" + elapsed);
  image.style.transform = "translateY(" + (-42-8*reveal) + "%) scale(" + (0.95+reveal*0.05) + ")";
  image.style.opacity = reveal;
}

const compactFrames = [
  { at: 12, image: "capsule-light", title: "把待办收成胶囊。", caption: "晨光 · 浅色", background: "#efefeb", x: 50, y: 65, width: 44 },
  { at: 13, image: "capsule", title: "把待办收成胶囊。", caption: "夜色 · 深色", background: "#e5e7ef", x: 50, y: 65, width: 44 },
  { at: 14, image: "capsule-forest", title: "把待办收成胶囊。", caption: "森林 · 绿色", background: "#e7eee7", x: 50, y: 65, width: 44 },
  { at: 15, image: "topbar", title: "贴在屏幕顶部。", caption: "待办、倒计时和最近日程，排成一行。", background: "#e7eee7", x: 50, y: 10, width: 82 },
  { at: 17, image: "taskbar", title: "移进任务栏。", caption: "点击进度面板，即可展开待办。", background: "#e7eaf7", x: 20, y: 84, width: 21 },
  { at: 18.5, image: "taskbar", title: "完全隐藏。", caption: "双击托盘图标，随时展开。", background: "#efefeb", x: 89, y: 84, width: 2 }
];

function showCompact(t) {
  const index = compactFrames.findLastIndex(frame => t >= frame.at);
  const current = compactFrames[index];
  const previous = compactFrames[Math.max(0,index-1)];
  const progress = smooth((t-current.at)/0.4);
  const blend = (from,to) => from + (to-from)*progress;
  const hidden = current.at === 18.5;
  background(current.background);
  byId("bar-line").style.opacity = clamp((t-17)/0.4);
  byId("tray-mark").style.opacity = hidden ? progress : 0;
  const mini = byId("miniature");
  setImage(byId("compact-image"), progress < 0.5 ? previous.image : current.image);
  byId("compact-title").textContent = current.title;
  byId("compact-caption").textContent = current.caption;
  byId("compact").querySelector(".center-copy").style.top = (current.image === "topbar" ? 41 : 20) + "%";
  mini.style.width = blend(previous.width,current.width) + "%";
  mini.style.left = blend(previous.x,current.x) + "%";
  mini.style.top = blend(previous.y,current.y) + "%";
  const enter = ease((t-12)/0.4);
  mini.style.transform = "translate(-50%,-50%) scale(" + (1.32-0.32*enter) + ")";
  mini.style.opacity = hidden ? 1-progress : enter;
}

function showPerformance(t) {
  background("#edf1f8");
  const enter = ease((t-20)/0.45);
  const layout = document.querySelector(".perf-layout");
  layout.style.opacity = enter;
  layout.style.transform = "translateY(" + (1-enter)*4 + "%)";
}

function showQuiet(t) {
  const enter = smooth((t-25.5)/0.5);
  const leave = smooth((t-29)/0.5);
  const game = enter * (1-leave);
  background("#eeefeb");
  byId("game-menu").style.opacity = game;
  byId("game-shade").style.opacity = game;
  const back = t >= 29;
  byId("quiet-title").textContent = back ? "回到桌面。" : "打开 CS2。";
  byId("quiet-caption").textContent = back ? "待办自动恢复，继续刚才的进度。" : "游戏进入前台，待办自动隐藏。";
  byId("quiet").style.color = game > 0.65 ? "#ffffff" : "#191a1d";
  const image = byId("quiet-window");
  image.style.opacity = 1-game;
  image.style.transform = "translate(" + game*15 + "%,-50%) scale(" + (1-0.08*game) + ")";
}

function render(t) {
  position = Math.min(duration, Math.max(0,t));
  const ratio = canvas.width / 1600;
  ctx.setTransform(ratio,0,0,canvas.height/900,0,0);
  if (t < 4) {
    showShot("intro"); background(paper); introArt(t);
    byId("cover-action").style.visibility = audio.paused && t < 0.1 ? "visible" : "hidden";
    byId("intro").style.opacity = 1-smooth((t-2.75)/0.65);
  } else if (t < 8) {
    showShot("work"); showWork(t);
  } else if (t < 12) {
    showShot("focus"); showFocus(t);
  } else if (t < 20) {
    showShot("compact"); showCompact(t);
  } else if (t < 25) {
    showShot("performance"); showPerformance(t);
  } else if (t < 32) {
    showShot("quiet"); showQuiet(t);
  } else {
    showShot("end"); background(paper);
    const reveal = ease((t-32)/0.65);
    byId("end").style.opacity = reveal;
    byId("end").style.transform = "translateY(" + (1-reveal)*5 + "%)";
  }
  seek.value = position;
  seek.style.setProperty("--progress", (100*position/duration) + "%");
  byId("elapsed").textContent = timecode(position);
  const current = chapters.findLastIndex(button => position >= Number(button.dataset.at));
  if (current !== chapterIndex) {
    chapterIndex = current;
    for (const [index, button] of chapters.entries()) button.setAttribute("aria-current", String(index===current));
    byId("chapter-name").textContent = chapters[current].lastChild.textContent;
  }
}

function sizeFrame() {
  const box = stage.getBoundingClientRect();
  const density = Math.min(devicePixelRatio, 2);
  canvas.width = Math.max(1, Math.round(box.width * density));
  canvas.height = Math.max(1, Math.round(box.height * density));
  render(position);
}

function frame() {
  render(audio.currentTime);
  if (!audio.paused) animation = requestAnimationFrame(frame);
}

function playbackState() {
  const playing = !audio.paused;
  const label = playing ? "暂停" : position >= duration ? "重新播放" : "播放";
  play.firstElementChild.className = playing ? "pause-icon" : "triangle";
  play.setAttribute("aria-label",label);
  play.title = label + "（空格）";
  cancelAnimationFrame(animation);
  frame();
}

function moveTo(seconds) {
  audio.currentTime = Math.max(0, Math.min(duration, seconds));
  render(audio.currentTime);
}

async function togglePlayback() {
  if (!audio.paused) { audio.pause(); return; }
  if (audio.ended || position >= duration) moveTo(0);
  await audio.play();
}

function toggleSound() {
  audio.muted = !audio.muted;
  const label = audio.muted ? "开启声音" : "静音";
  byId("sound").setAttribute("aria-pressed",String(audio.muted));
  byId("sound").setAttribute("aria-label",label);
  byId("sound").title = label + "（M）";
}

async function toggleFullscreen() {
  if (document.fullscreenElement) await document.exitFullscreen();
  else await player.requestFullscreen();
}

function wakeControls() {
  clearTimeout(controlsTimer);
  const transport = document.querySelector(".transport");
  transport.classList.remove("sleeping");
  if (document.fullscreenElement && !audio.paused) controlsTimer = setTimeout(() => transport.classList.add("sleeping"), 2200);
}

byId("watch").addEventListener("click", event => { event.stopPropagation(); togglePlayback(); });
stage.addEventListener("click", togglePlayback);
play.addEventListener("click",togglePlayback);
byId("sound").addEventListener("click",toggleSound);
byId("sound").setAttribute("aria-pressed","false");
byId("expand").hidden = !document.fullscreenEnabled;
byId("expand").addEventListener("click",toggleFullscreen);
seek.addEventListener("input", () => moveTo(Number(seek.value)));
for (const button of chapters) button.addEventListener("click", () => moveTo(Number(button.dataset.at)));
audio.addEventListener("play",playbackState);
audio.addEventListener("pause",playbackState);
audio.addEventListener("ended",playbackState);
audio.addEventListener("seeked", () => render(audio.currentTime));
audio.addEventListener("error", () => { byId("status").textContent = "配乐文件读取失败，请检查 assets/score.wav。"; });
player.addEventListener("pointermove",wakeControls);
audio.addEventListener("play",wakeControls);
audio.addEventListener("pause",wakeControls);
document.addEventListener("fullscreenchange", () => {
  const label = document.fullscreenElement ? "退出全屏" : "进入全屏";
  byId("expand").setAttribute("aria-label",label);
  byId("expand").title = label + "（F）";
  wakeControls();
});
document.addEventListener("keydown", event => {
  if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
  if (event.code === "Space" && !event.target.closest("button,input")) { event.preventDefault(); togglePlayback(); }
  if (event.code === "KeyM") toggleSound();
  if (event.code === "KeyF" && document.fullscreenEnabled) toggleFullscreen();
  if (event.target !== seek && ["ArrowLeft","ArrowRight"].includes(event.code)) {
    event.preventDefault(); moveTo(position + (event.code === "ArrowRight" ? 2 : -2));
  }
});
new ResizeObserver(sizeFrame).observe(stage);
sizeFrame();
