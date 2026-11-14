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
const assets = Object.fromEntries(["tasks","completed","schedule","focus","running-0","running-1","running-2","running-3","capsule","taskbar"].map(name => {
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
  const schedule = t >= 10;
  const local = t - (schedule ? 10 : 4);
  background(schedule ? "#e5eafa" : "#f7f7f3");
  ctx.fillStyle = schedule ? "#ced9fa" : "#e8eafa";
  ctx.beginPath();
  ctx.arc(1550, 890, 705 + 25*ease(local/3), 0, 2*Math.PI);
  ctx.fill();
  byId("work-title").textContent = schedule ? "把时间\n也记下来。" : "一件待办，\n一次勾选。";
  byId("work-caption").textContent = schedule ? "每天 16:00，提醒下午看片。" : "按下 Enter 保存，点击方框完成。";
  setImage(byId("work-image"), schedule ? "schedule" : t < 7 ? "tasks" : "completed");
  const window = byId("native");
  const enter = ease(local / 0.55);
  const inspect = schedule ? 0 : smooth((t-5.5)/0.75)*(1-smooth((t-8.25)/0.75));
  window.style.transform = "translate(" + (1-enter)*22 + "%,-50%) scale(" + (0.96+0.04*enter+inspect*0.19) + ")";
  window.style.opacity = enter;
  byId("work").querySelector(".shot-copy").style.opacity = clamp(local/0.16);
  const ring = byId("tap");
  const ripple = clamp((t-7)/0.5);
  ring.style.opacity = t >= 7 && t < 7.5 ? 1-ripple : 0;
  ring.style.transform = "translate(-50%,-50%) scale(" + (0.35+ripple) + ")";
}

function showFocus(t) {
  background("#171a23");
  const enter = ease((t-14)/0.55);
  const reveal = ease((t-15)/0.8);
  ctx.save();
  ctx.strokeStyle = "#34394b";
  ctx.lineWidth = 2;
  for (let i=0; i<60; i++) {
    const angle = (i-15)*Math.PI/30;
    const x = Math.cos(angle), y = Math.sin(angle);
    ctx.strokeStyle = i < Math.floor((t-14)*2) ? "#9aaaff" : "#34394b";
    ctx.beginPath();
    ctx.moveTo(1165+x*371,448+y*371);
    ctx.lineTo(1165+x*(i%5===0?340:356),448+y*(i%5===0?340:356));
    ctx.stroke();
  }
  ctx.restore();
  byId("focus-type").style.transform = "translate(" + (1-enter)*12 + "%,-50%)";
  const image = document.querySelector(".native-focus");
  const elapsed = Math.max(0, Math.floor(t-16));
  const remaining = 1500-elapsed;
  byId("minutes").textContent = Math.floor(remaining/60);
  byId("seconds").textContent = String(remaining%60).padStart(2,"0");
  setImage(image, t < 16 ? "focus" : "running-" + elapsed);
  image.style.transform = "translateY(" + (-42-8*reveal) + "%) scale(" + (0.95+reveal*0.05) + ")";
  image.style.opacity = reveal;
}

function showCompact(t) {
  background("#e7eaf7");
  const morph = smooth((t-22.5)/0.5);
  const dock = byId("bar-line");
  dock.style.opacity = morph;
  const mini = byId("miniature");
  setImage(byId("compact-image"), morph < 0.5 ? "capsule" : "taskbar");
  byId("compact-title").textContent = morph < 0.5 ? "收成一枚胶囊。" : "也能放进任务栏。";
  mini.style.width = (48-27*morph) + "%";
  mini.style.left = (50-30*morph) + "%";
  mini.style.top = (64+20*morph) + "%";
  const enter = ease((t-20)/0.6);
  mini.style.transform = "translate(-50%,-50%) scale(" + (1.32-0.32*enter) + ")";
  mini.style.opacity = enter;
}

function showQuiet(t) {
  const enter = smooth((t-25.85)/0.65);
  const leave = smooth((t-28.75)/0.55);
  const game = enter * (1-leave);
  background("#eeefeb");
  if (game > 0) {
    ctx.save();
    ctx.beginPath(); ctx.rect(1600*(1-game),0,1600,900); ctx.clip();
    background("#1d2d8c");
    // 几何山体标示前台场景，窗口在切换时退出画面。
    for (const [y, color, shift] of [[520,"#3b55cd",0],[685,"#627bf2",200],[830,"#a0b0ff",100]]) {
      ctx.fillStyle=color;
      ctx.beginPath(); ctx.moveTo(0,900); ctx.lineTo(0,y+200);
      ctx.lineTo(950+shift-22*(t-26),y-125); ctx.lineTo(1150+shift,y-32); ctx.lineTo(1600,y-220);
      ctx.lineTo(1600,900); ctx.fill();
    }
    ctx.restore();
  }
  const back = t >= 29;
  byId("quiet-title").textContent = back ? "回到桌面。" : "游戏开始。";
  byId("quiet-caption").textContent = back ? "待办自动恢复，继续刚才的进度。" : "指定游戏进入前台，窗口自动隐藏。";
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
  } else if (t < 14) {
    showShot("work"); showWork(t);
  } else if (t < 20) {
    showShot("focus"); showFocus(t);
  } else if (t < 25) {
    showShot("compact"); showCompact(t);
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
