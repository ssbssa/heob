const SHOW_SUM_COUNT = 100;

var headerHeight = 50;
var footerHeight = 90;
var spacer = 10;

var maxStack = 0;
var sampleTimes = 0;
var fullWidth = 0;
var halfWidth = 0;
var functionText;
var sourceText;
var addressText;
var infoText;
var threadText;
var functionTextReset = '';
var sourceTextReset = '';
var addressTextReset = '';
var infoTextReset = '';
var threadTextReset = '';
var lastZoomers;
var mapType;
var suppressStacks = 0;
var searchRe;

var addrCountMap = new Map();
var sourceCountMap = new Map();
var funcCountMap = new Map();
var addrCountArr;
var sourceCountArr;
var funcCountArr;
var maxCount;
var showMap;
var showArr;
var showKey;

var threadMap = new Map();
var threadArray = new Array();

function heobInit()
{
  let svgs = document.getElementsByTagName('svg');
  let svgNs = svgs[0].getAttribute('xmlns');

  let rect0 = addRectPara(svgNs, '100%', '100%', '#cccccc');
  svgs[0].insertBefore(rect0, svgs[0].firstChild);

  svgs[0].onmousedown = function (e) { if (e.button === 1) return false; };

  maxStack = -1;
  let bottom, zoomSvg;
  let firstSvg;
  let minStack = 0;
  let sumSamples = 0;
  let sumAllocs = 0;
  let colorMapSource = new Map();
  let colorMapFunc = new Map();
  let colorMapAddr = new Map();
  let colorMapBlocked = new Map();
  let colorMapThread = new Map();
  let blockedCount = 0;

  let sumAll = 0;
  let thread1Ofs = 0;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] === undefined) continue;

    let samples = parseInt(svg.attributes['heobSum'].value);
    let svgType = svg.attributes['heobAllocs'] === undefined;

    if (svgType && svg.attributes['heobThread'] !== undefined)
    {
      let thread = parseInt(svg.attributes['heobThread'].value.substring(7));
      let ofs = parseInt(svg.attributes['heobOfs'].value);
      if (thread === 1 && ofs + 0.1 >= thread1Ofs)
      {
        sampleTimes += samples;
        thread1Ofs = ofs + samples;
      }
    }

    let stack = parseInt(svg.attributes['heobStack'].value);
    if (stack !== 1) continue;

    sumAll += samples;
  }
  if (sumAll > 0)
  {
    let allSvg = document.createElementNS(svgNs, 'svg');
    allSvg.setAttribute('heobSum', sumAll);
    allSvg.setAttribute('heobOfs', 0);
    allSvg.setAttribute('heobStack', 0);
    allSvg.setAttribute('heobFunc', 'all');
    svgs[0].appendChild(allSvg);
  }

  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] === undefined) continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);
    let allocs = 0;
    let svgType = svg.attributes['heobAllocs'] === undefined;
    if (!svgType)
      allocs = parseInt(svg.attributes['heobAllocs'].value);
    if (mapType === undefined)
      mapType = svgType;

    if (firstSvg === undefined && stack > 0)
      firstSvg = svg;

    if (stack === 1)
    {
      if (svgType)
        sumSamples += samples;
      sumAllocs += allocs;
    }

    if (stack === 1 && zoomSvg === undefined)
      zoomSvg = svg;
    else if (stack === 0)
    {
      bottom = svg;
      if (samples > parseInt(zoomSvg.attributes['heobSum'].value) + 0.1)
        zoomSvg = svg;
      else
        minStack = 1;

      svg.setAttribute('heobText',
          sumText(sumSamples, samples - sumSamples, sumAllocs));
    }

    if (stack > maxStack) maxStack = stack;

    if (stack > 1)
    {
      if (svg.attributes['heobAddr'] !== undefined &&
          svg.attributes['heobMod'] !== undefined)
      {
        let addrKey = svg.attributes['heobAddr'].value;
        addToCountMap(addrKey, addrCountMap, ofs, samples, svg);
      }
      if (svg.attributes['heobSource'] !== undefined)
      {
        let sourceKey = svg.attributes['heobSource'].value;
        addToCountMap(sourceKey, sourceCountMap, ofs, samples, svg);
      }
      if (svg.attributes['heobFunc'] !== undefined)
      {
        let funcKey = svg.attributes['heobFunc'].value;
        addToCountMap(funcKey, funcCountMap, ofs, samples, svg);
      }
    }

    let color;
    if (stack <= 1)
      color = createBaseColor();
    else if (svg.attributes['heobSource'] !== undefined)
    {
      color = getColorOfMap(svg,
          colorMapSource, 'heobFunc', createSourceColor);
      if (svg.attributes['heobAddr'] === undefined)
        color = '#' +
          (parseInt(color.substring(1), 16) + 0x004000).toString(16);
    }
    else if (svg.attributes['heobFunc'] !== undefined)
      color = getColorOfMap(svg,
          colorMapFunc, 'heobFunc', createFuncColor);
    else if (svg.attributes['heobAddr'] !== undefined)
      color = getColorOfMap(svg,
          colorMapAddr, 'heobAddr', createAddrColor);
    else if (svg.attributes['heobBlocked'] !== undefined)
      color = getColorOfMap(svg,
          colorMapBlocked, 'heobThread', createBlockedColor);
    else
      color = getColorOfMap(svg,
          colorMapThread, 'heobThread', createThreadColor);

    svg.setAttribute('class', 'sample');
    svg.setAttribute('onmouseover', 'infoSet(this)');
    svg.setAttribute('onmouseout', 'infoClear()');
    svg.setAttribute('onclick', 'zoom(evt, this)');
    svg.setAttribute('heobColor', color);
    svg.setAttribute('onmousedown', 'delZoom(evt, this)');

    addTitle(svg, svgNs, '');

    addRectBg(svg, svgNs, rect0);

    addRect(svg, svgNs, color);

    let t;
    if (svg.attributes['heobFunc'] !== undefined)
      t = svg.attributes['heobFunc'].value;
    else if (svg.attributes['heobSource'] !== undefined)
      t = svg.attributes['heobSource'].value;
    else if (svg.attributes['heobMod'] !== undefined)
    {
      t = svg.attributes['heobMod'].value;
      let delim = t.lastIndexOf('\\');
      if (delim >= 0)
        t = t.substr(delim + 1);
    }
    else if (svg.attributes['heobAddr'] !== undefined)
      t = svg.attributes['heobAddr'].value;
    else if (svg.attributes['heobThread'] !== undefined)
      t = svg.attributes['heobThread'].value;
    else
      continue;
    addText(svg, svgNs, t);

    if (svg.attributes['heobBlocked'] !== undefined)
      blockedCount++;

    if (svg.attributes['heobThread'] !== undefined)
    {
      let thread = svg.attributes['heobThread'].value;
      let mapEntry = threadMap.get(thread);
      if (mapEntry === undefined)
      {
        mapEntry = new Array(5);
        mapEntry[0] = 0;           // sum of samples
        mapEntry[1] = 0;           // maximum extention
        mapEntry[2] = svg;         // data reference
        mapEntry[3] = 0;           // sum of allocation count
        mapEntry[4] = undefined;   // color
        threadMap.set(thread, mapEntry);
      }
      if (ofs + 0.1 >= mapEntry[1] && mapType === svgType)
      {
        if (mapEntry[0] && mapEntry[4] === undefined)
          mapEntry[4] = getColorOfMap(svg,
              colorMapThread, 'heobThread', createThreadColor);
        mapEntry[0] += samples;
        mapEntry[1] = ofs + samples;
        mapEntry[3] += allocs;
      }
    }
  }
  maxStack++;
  if (minStack > 0)
  {
    svgs[0].removeChild(bottom);
    bottom = undefined;
    for (let i = 0; i < svgs.length; i++)
    {
      let svg = svgs[i];
      if (svg.attributes['heobStack'] === undefined) continue;

      svg.setAttribute('heobStack',
          parseInt(svg.attributes['heobStack'].value) - 1);
    }
    maxStack--;
  }
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] === undefined) continue;

    let t = withNL(funcAttribute(svg)) + withNL(sourceAttribute(svg, true)) +
      withNL(addrModAttribute(svg, true)) + withNL(sumAttribute(svg)) +
      withNL(threadAttribute(svg)) + withNL(idAttribute(svg));
    svg.getElementsByTagName('title')[0].textContent = t;
  }

  let svg = svgs[0];

  if (firstSvg !== undefined && bottom !== undefined)
    svg.insertBefore(bottom, firstSvg);

  let svgWidth = parseInt(svg.width.baseVal.value);
  fullWidth = svgWidth - 2 * spacer;
  let fullHeight = headerHeight + maxStack * 16 + footerHeight;

  addrCountArr = arrayFromCountMap(addrCountMap);
  sourceCountArr = arrayFromCountMap(sourceCountMap);
  funcCountArr = arrayFromCountMap(funcCountMap);
  maxCount = Math.max(addrCountArr.length,
      sourceCountArr.length, funcCountArr.length);
  maxCount = Math.min(maxCount, SHOW_SUM_COUNT);

  threadMap.forEach(
    function (value, key, map)
    {
      if (value[0] > 0)
        threadArray.push(key);
    });
  threadSort();
  if (threadArray.length === 1)
    threadArray.pop();

  let extraCount = Math.max(maxCount, threadArray.length);
  let svgHeight = fullHeight;
  if (extraCount > 0 || blockedCount > 0)
    svgHeight += 40;
  if (extraCount > 0)
    svgHeight += extraCount * 16;
  svg.setAttribute('height', svgHeight);
  svg.setAttribute('viewBox', '0 0 ' + svgWidth + ' ' + svgHeight);

  halfWidth = fullWidth;
  if (maxCount > 0 && threadArray.length > 0)
    halfWidth = (fullWidth - spacer) / 2;

  let plusMinusY = headerHeight + (maxStack - 2) * 16 + 3.5;
  addPlusMinus(svg, svgNs, 1, plusMinusY, 0);
  addPlusMinus(svg, svgNs, 1, plusMinusY, -1);
  for (let i = 4; i < maxStack; i++)
    addPlusMinus(svg, svgNs, 1, 0, i);

  addDistSvgs(svgNs, svg, maxCount, 'addr', 0, fullHeight, rect0);

  addDistSvgs(svgNs, svg, threadArray.length, 'thread',
    fullWidth - halfWidth, fullHeight, rect0);

  let textWidth = 120;
  let blockedSvg = createSvg(svgNs,
      svgWidth * 3 / 4 - textWidth / 2, fullHeight, textWidth, 16, 'blocked');
  addRectBg(blockedSvg, svgNs, rect0);
  createGradient(svg, svgNs, 'grad_blocked', '#20ff20', '#9eff9e');
  addRect(blockedSvg, svgNs, 'url(#grad_blocked)');
  let blockedText = addText(blockedSvg, svgNs, 'idle', textWidth / 2);
  blockedText.setAttribute('text-anchor', 'middle');
  blockedSvg.setAttribute('onmouseover', 'blockedInfoSet()');
  blockedSvg.setAttribute('onmouseout', 'addrInfoClear()');
  blockedSvg.setAttribute('onclick', 'blockedZoom(evt)');
  blockedSvg.setAttribute('onmousedown', 'blockedDelZoom(evt)');
  svg.appendChild(blockedSvg);

  let grad1 = ['#3fa0a0', '#a03fa0', '#a0a03f'];
  let grad2 = ['#3fdfdf', '#df3fdf', '#dfdf3f'];
  let showTexts = ['function', 'source', 'address'];
  for (let i = 0; i < 3; i++)
  {
    let showTypeSvg = createSvg(svgNs,
        svgWidth / 4 + (textWidth + spacer) * (i - 1) - textWidth / 2,
        fullHeight, textWidth, 16, 'showType' + i);
    addRectBg(showTypeSvg, svgNs, rect0);
    createGradient(svg, svgNs, 'grad_' + showTexts[i], grad1[i], grad2[i]);
    addRect(showTypeSvg, svgNs, 'url(#grad_' + showTexts[i] + ')');
    let showText = addText(showTypeSvg, svgNs, showTexts[i], textWidth / 2);
    showText.setAttribute('text-anchor', 'middle');
    showTypeSvg.setAttribute('onclick', 'showType(' + i + ', 1)');
    svg.appendChild(showTypeSvg);
  }

  functionText = addInfoText(svg, svgNs);
  sourceText = addInfoText(svg, svgNs);
  addressText = addInfoText(svg, svgNs);
  infoText = addInfoText(svg, svgNs);
  threadText = addInfoText(svg, svgNs);

  let texts = [functionText, sourceText, addressText, infoText, threadText];
  let tx = spacer + 2;
  let ty = fullHeight - 70;
  for (let i = 0; i < texts.length; i++)
  {
    texts[i].setAttribute('x', tx);
    texts[i].setAttribute('y', ty);
    ty += 15;
  }

  let cmdText = document.getElementById('cmd');
  cmdText.setAttribute('text-anchor', 'middle');
  cmdText.setAttribute('x', '50%');
  cmdText.setAttribute('y', '24');
  cmdText.setAttribute('font-size', '17');
  cmdText.setAttribute('font-family', 'Verdana');
  cmdText.setAttribute('onclick', 'alert(this.attributes["heobCmd"].value)');
  addTitle(cmdText, svgNs, cmdText.attributes['heobCmd'].value);

  showType(0, 0);

  if (zoomSvg !== undefined)
  {
    // fake left mouse button
    let e = { button: 0 };
    zoom(e, zoomSvg);
  }
  infoClear();

  window.addEventListener("keydown",
      function (e)
      {
        if (e.keyCode === 114 || (e.ctrlKey && e.keyCode === 70))
        {
          e.preventDefault();
          searchFunction();
        }
      });
}

function threadSort()
{
  threadArray.sort(
    function (a, b)
    {
      let sumSamplesA = threadMap.get(a)[0];
      let sumSamplesB = threadMap.get(b)[0];
      if (sumSamplesA !== sumSamplesB)
        return sumSamplesB - sumSamplesA;
      return a.localeCompare(b, undefined, {numeric: true});
    });
}

function addDistSvgs(svgNs, par, count, name, xOfs, fullHeight, rect0)
{
  for (let i = 0; i < count; i++)
  {
    let x = spacer + xOfs;
    let y = fullHeight + 31 + i * 16;

    let newSvg = createSvg(svgNs, x, y, halfWidth, 16, name + i);

    addTitle(newSvg, svgNs, '');

    addRectBg(newSvg, svgNs, rect0);

    addRect(newSvg, svgNs);

    addText(newSvg, svgNs, '');

    newSvg.setAttribute('onmouseover', name + 'InfoSet(this)');
    newSvg.setAttribute('onmouseout', 'addrInfoClear()');
    newSvg.setAttribute('onclick', name + 'Zoom(evt, this)');
    newSvg.setAttribute('onmousedown', name + 'DelZoom(evt, this)');

    par.appendChild(newSvg);
  }
}

function searchFunction()
{
  if (searchRe !== undefined)
    searchRe = undefined;
  else
  {
    let str = prompt("Search string", "");
    if (str !== null)
      searchRe = new RegExp(str, 'i');
  }

  let svgs = document.getElementsByTagName('svg');
  let found = false;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobFunc'] === undefined) continue;
    let rect = svg.getElementsByTagName('rect')[1];
    if (searchRe === undefined ||
        svg.attributes['heobFunc'].value.search(searchRe) < 0)
    {
      rect.setAttribute('fill', svg.attributes['heobColor'].value);
      continue;
    }
    rect.setAttribute('fill', '#ffffff');
    found = true;
  }

  if (!found) searchRe = undefined;

  showTypeData();
}

function createGradient(svg, svgNs, name, grad1, grad2)
{
  let newDefs = document.createElementNS(svgNs, 'defs');
  let newGrad = document.createElementNS(svgNs, 'linearGradient');
  newGrad.setAttribute('id', name);
  let newStop = document.createElementNS(svgNs, 'stop');
  newStop.setAttribute('offset', '5%');
  newStop.setAttribute('stop-color', grad1);
  newGrad.appendChild(newStop);
  newStop = document.createElementNS(svgNs, 'stop');
  newStop.setAttribute('offset', '95%');
  newStop.setAttribute('stop-color', grad2);
  newGrad.appendChild(newStop);
  newDefs.appendChild(newGrad);
  svg.appendChild(newDefs);
}

function getColorOfMap(svg, map, attr, colorFunction)
{
  let color;
  if (svg.attributes[attr] !== undefined)
    color = map.get(svg.attributes[attr].value);
  if (color === undefined)
  {
    color = colorFunction();
    if (svg.attributes[attr] !== undefined)
      map.set(svg.attributes[attr].value, color);
  }
  return color;
}

function addToCountMap(key, map, ofs, samples, svg)
{
  let val = map.get(key);
  if (val === undefined)
  {
    val = new Array(7);
    val[0] = 0;   // count
    val[1] = 0;   // maximum extention
    val[2] = 0;   // sum of samples
    val[3] = 0;   // sum of allocation count
    val[4] = svg; // data reference
    val[5] = 0;   // use source of data reference
    val[6] = 0;   // use address of data reference
    map.set(key, val);
  }
  if (ofs + 0.1 >= val[1])
  {
    val[0]++;
    val[1] = ofs + samples;
    val[2] += samples;
  }
}

function arrayFromCountMap(map)
{
  let arr = new Array();
  map.forEach(
    function (val, key, map)
    {
      if (val[2] > 0 && val[0] >= 2)
        arr.push(key);
    });
  return arr;
}

function resetCountMap(map)
{
  map.forEach(
    function (val, key, map)
    {
      val[0] = 0;
      val[1] = 0;
      val[2] = 0;
      val[3] = 0;
      val[5] = val[4].attributes['heobSource'] !== undefined ? 1 : 0;
      val[6] = val[4].attributes['heobAddr'] !== undefined ? 1 : 0;
    });
}

function updateCountMap(map, svg, keyName, ofs, samples, shownSamples, allocs)
{
  if (svg.attributes[keyName] === undefined) return;
  let key = svg.attributes[keyName].value;
  let val = map.get(key);
  if (val === undefined || ofs + 0.1 < val[1]) return;
  val[0]++;
  val[1] = ofs + samples;
  val[2] += shownSamples;
  val[3] += allocs;
  if (val[5] &&
      (svg.attributes['heobSource'] === undefined ||
       svg.attributes['heobSource'].value !==
       val[4].attributes['heobSource'].value))
    val[5] = 0;
  if (val[6] &&
      (svg.attributes['heobAddr'] === undefined ||
       svg.attributes['heobAddr'].value !==
       val[4].attributes['heobAddr'].value))
    val[6] = 0;
}

function sortCountMap(map, arr)
{
  arr.sort(
    function (a, b)
    {
      let valueA = map.get(a);
      let valueB = map.get(b);
      if ((valueA[0] >= 2) === (valueB[0] >= 2))
        return valueB[2] - valueA[2];
      return valueA[0] >= 2 ? -1 : 1;
    });
}

function getShowKey(svg)
{
  let key;
  if (svg.attributes[showKey] !== undefined)
    key = svg.attributes[showKey].value;
  return key;
}

function createColor(colorFactor, colorOfs, colorSummand)
{
  let colorNum =
    colorFactor * (colorOfs + Math.floor(Math.random() * 64)) +
    colorSummand;
  return '#' + colorNum.toString(16);
}

function createBaseColor()
{
  return createColor(0x000202, 0x10, 0xff0000);
}

function createSourceColor()
{
  return createColor(0x010001, 0xa0, 0x003f00);
}

function createFuncColor()
{
  return createColor(0x000101, 0xa0, 0x3f0000);
}

function createAddrColor()
{
  return createColor(0x010100, 0xa0, 0x00003f);
}

function createThreadColor()
{
  return createColor(0x020200, 0x10, 0x0000ff);
}

function createBlockedColor()
{
  return createColor(0x020002, 0x10, 0x00ff00);
}

function createSvg(svgNs, x, y, width, height, id)
{
  let newSvg = document.createElementNS(svgNs, 'svg');
  newSvg.setAttribute('class', 'sample');
  newSvg.setAttribute('x', x);
  newSvg.setAttribute('y', y);
  newSvg.setAttribute('width', width);
  newSvg.setAttribute('height', height);
  newSvg.setAttribute('id', id);
  newSvg.style['display'] = 'block';
  return newSvg;
}

function addTitle(par, svgNs, t)
{
  let newTitle = document.createElementNS(svgNs, 'title');
  newTitle.textContent = t;
  par.appendChild(newTitle);
}

function addText(par, svgNs, t, x)
{
  let newText = document.createElementNS(svgNs, 'text');
  if (x === undefined)
    x = '2';
  newText.setAttribute('x', x);
  newText.setAttribute('y', '10.5');
  newText.setAttribute('font-size', '12');
  newText.setAttribute('font-family', 'Verdana');
  newText.textContent = t;
  par.appendChild(newText);
  return newText;
}

function addInfoText(par, svgNs)
{
  let newText = document.createElementNS(svgNs, 'text');
  newText.setAttribute('font-size', '12');
  newText.setAttribute('font-family', 'Verdana');
  newText.setAttributeNS('http://www.w3.org/XML/1998/namespace',
      'xml:space', 'preserve');
  par.appendChild(newText);
  return newText;
}

function addRectPara(svgNs, width, height, color)
{
  let newRect = document.createElementNS(svgNs, 'rect');
  newRect.setAttribute('width', width);
  newRect.setAttribute('height', height);
  newRect.setAttribute('fill', color);
  return newRect;
}

function addRectBg(par, svgNs, rect0)
{
  let newRect = addRectPara(svgNs,
      '100%', '16', rect0.attributes['fill'].value);
  newRect.setAttribute('stroke-width', '0');
  par.appendChild(newRect);
}

function addRect(par, svgNs, color)
{
  let newRect = addRectPara(svgNs, '100%', '15', color);
  newRect.setAttribute('rx', '3');
  newRect.setAttribute('ry', '3');
  par.appendChild(newRect);
}

function addLine(par, svgNs, x1, y1, x2, y2, color)
{
  let line = document.createElementNS(svgNs, 'line');
  line.setAttribute('x1', x1);
  line.setAttribute('y1', y1);
  line.setAttribute('x2', x2);
  line.setAttribute('y2', y2);
  line.setAttribute('stroke', color);
  par.appendChild(line);
}

function addPlusMinus(par, svgNs, x, y, stack)
{
  let svg = createSvg(svgNs, x, y, 8, 8,
      (stack ? 'minus' : 'plus') + (stack > 0 ? stack : ''));
  svg.appendChild(addRectPara(svgNs, '100%', '100%', '#ffffff'));
  addLine(svg, svgNs, 1, 4, 7, 4, 'black');
  if (!stack)
    addLine(svg, svgNs, 4, 1, 4, 7, 'black');
  svg.setAttribute('onclick', 'plusMinus(' + stack + ')');
  if (!stack)
    svg.style['display'] = 'none';
  addTitle(svg, svgNs,
      stack === 0 ? 'show all stack frames' :
      (stack < 0 ? 'suppress irrelevant stack frames' :
       'suppress stack frames'));
  par.appendChild(svg);
}

function plusMinus(stack)
{
  suppressStacks = stack;
  let minusSvg = document.getElementById('minus');
  minusSvg.style['display'] = !stack ? 'block' : 'none';
  let plusSvg = document.getElementById('plus');
  plusSvg.style['display'] = !stack ? 'none' : 'block';
  zoomArr(lastZoomers);
}

function attributeToText(svg, attr, noPath)
{
  if (svg.attributes[attr] !== undefined)
  {
    let t = svg.attributes[attr].value;
    if (noPath === true)
    {
      let delim = t.lastIndexOf('\\');
      if (delim >= 0)
        t = t.substr(delim + 1);
    }
    return t;
  }
  else
    return '';
}

function funcAttribute(svg)
{
  return attributeToText(svg, 'heobFunc');
}

function sourceAttribute(svg, noPath)
{
  return attributeToText(svg, 'heobSource', noPath);
}

function addrModAttribute(svg, noPath)
{
  let tAddr = attributeToText(svg, 'heobAddr');
  let tMod = attributeToText(svg, 'heobMod', noPath);
  if (tAddr.length === 0 && tMod.length > 0)
    tAddr = 'inlined';
  if (tAddr.length > 0 && tMod.length > 0)
    return tAddr + ': ' + tMod;
  else
    return tAddr;
}

function sumText(samples, bytes, allocs)
{
  let t = '';
  if (samples > 0.1)
    t += samples + ' sample' + (samples > 1.1 ? 's' : '') + ' (' +
        (100 * samples / sampleTimes).toFixed(2) + '%)';
  if (samples > 0.1 && bytes > 0.1)
    t += '  &  ';
  if (bytes > 0.1)
  {
    t += bytes + ' B';
    if (allocs > 0.1)
      t += ' / ' + allocs;
  }
  return t;
}

function sumAttribute(svg)
{
  if (svg.attributes['heobText'] !== undefined)
    return svg.attributes['heobText'].value;

  let sum = parseInt(svg.attributes['heobSum'].value);
  if (svg.attributes['heobAllocs'] !== undefined)
    return sumText(0, sum, parseInt(svg.attributes['heobAllocs'].value));
  else
    return sumText(sum, 0, 0);
}

function threadAttribute(svg)
{
  return attributeToText(svg, 'heobThread');
}

function idAttribute(svg)
{
  return attributeToText(svg, 'heobId');
}

function withNL(t)
{
  if (t.length === 0)
    return t;
  return t + '\n';
}

function infoSet(svg)
{
  functionText.textContent = funcAttribute(svg);
  sourceText.textContent = sourceAttribute(svg);
  addressText.textContent = addrModAttribute(svg);
  infoText.textContent = sumAttribute(svg);
  threadText.textContent = threadAttribute(svg);
}

function infoClear()
{
  functionText.textContent = functionTextReset;
  sourceText.textContent = sourceTextReset;
  addressText.textContent = addressTextReset;
  infoText.textContent = infoTextReset;
  threadText.textContent = threadTextReset;
}

function opacitySetter(setter)
{
  let svgs = document.getElementsByTagName('svg');
  for (let i = 0; i < svgs.length; i++)
  {
    let svgEntry = svgs[i];
    if (svgEntry.attributes['heobSum'] === undefined) continue;
    if (svgEntry.style['display'] === 'none') continue;

    let opacity = parseFloat(svgEntry.attributes['heobOpacity'].value);
    if (setter !== undefined)
      opacity = setter(svgEntry, opacity);
    svgEntry.style['opacity'] = opacity;
  }
}

function addrInfoSet(svg)
{
  let key = svg.attributes['heobKey'].value;
  opacitySetter(
    function (svgEntry, opacity)
    {
      let entryKey = getShowKey(svgEntry);
      if (entryKey === undefined || entryKey !== key)
        opacity *= 0.25;
      else
        opacity = 1;
      return opacity;
    });
}

function addrInfoClear()
{
  opacitySetter();
}

function threadInfoSet(svg)
{
  let key = svg.attributes['heobKey'].value;
  opacitySetter(
    function (svgEntry, opacity)
    {
      if (svgEntry.attributes['heobThread'] === undefined ||
        svgEntry.attributes['heobThread'].value !== key)
        opacity *= 0.25;
      else
        opacity = 1;
      return opacity;
    });
}

function blockedInfoSet()
{
  opacitySetter(
    function (svgEntry, opacity)
    {
      if (svgEntry.attributes['heobBlocked'] === undefined)
        opacity *= 0.25;
      else
        opacity = 1;
      return opacity;
    });
}

function getZoomers(svg)
{
  let ofs = parseInt(svg.attributes['heobOfs'].value);
  let stack = parseInt(svg.attributes['heobStack'].value);
  let samples = parseInt(svg.attributes['heobSum'].value);

  let zoomers = new Array(1);
  let zoomer = new Array(3);
  zoomer[0] = ofs;
  zoomer[1] = stack;
  zoomer[2] = samples;
  zoomers[0] = zoomer;
  return zoomers;
}

function getSelZoomers(selector)
{
  let svgs = document.getElementsByTagName('svg');
  let zoomers = new Array();
  let maxExtention = 0;
  let foundSvg;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] === undefined) continue;

    if (!selector(svg))
      continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    if (ofs + 0.1 < maxExtention)
      continue;

    if (foundSvg === undefined)
      foundSvg = svg;

    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);

    maxExtention = ofs + samples;

    let zoomer = new Array(3);
    zoomer[0] = ofs;
    zoomer[1] = stack;
    zoomer[2] = samples;
    zoomers.push(zoomer);
  }
  return [zoomers, foundSvg];
}

function getAddrZoomers(key)
{
  return getSelZoomers(
    function (svg)
    {
      return getShowKey(svg) === key;
    });
}

function getThreadZoomers(key)
{
  return getSelZoomers(
    function (svg)
    {
      return svg.attributes['heobThread'] !== undefined &&
        svg.attributes['heobThread'].value === key;
    })[0];
}

function getBlockedZoomers()
{
  return getSelZoomers(
    function (svg)
    {
      return svg.attributes['heobBlocked'] !== undefined;
    })[0];
}

function zoomersAndNot(zoomers, invert)
{
  if (invert && lastZoomers.length > 0)
  {
    let left = lastZoomers[0][0];
    let last = lastZoomers[lastZoomers.length - 1];
    let right = last[0] + last[2];
    let invZoomers = new Array();
    for (let i = 0; i < zoomers.length; i++)
    {
      let z = zoomers[i];
      let ofs = z[0];
      let samples = z[2];

      if (left + 0.1 < ofs)
      {
        let zoomer = new Array(3);
        zoomer[0] = left;
        zoomer[1] = 0;
        zoomer[2] = ofs - left;
        invZoomers.push(zoomer);
      }

      left = ofs + samples;
    }
    if (left + 0.1 < right)
    {
      let zoomer = new Array(3);
      zoomer[0] = left;
      zoomer[1] = 0;
      zoomer[2] = right - left;
      invZoomers.push(zoomer);
    }
    zoomers = invZoomers;
  }

  for (let i = 0; i < lastZoomers.length; i++)
  {
    let lz = lastZoomers[i];
    let lofs = lz[0];
    let lsamples = lz[2];

    for (let j = 0; j < zoomers.length; j++)
    {
      let z = zoomers[j];
      let ofs = z[0];
      let samples = z[2];

      if (ofs + 0.1 > lofs + lsamples || ofs + samples - 0.1 < lofs)
        continue;

      if (ofs - 0.1 < lofs)
      {
        if (ofs + samples + 0.1 > lofs + lsamples)
        {
          // removed completely
          lastZoomers.splice(i, 1);
        }
        else
        {
          // left side was cut off
          lz[0] = ofs + samples;
          lz[2] = lofs + lsamples - lz[0];
        }
      }
      else
      {
        if (ofs + samples + 0.1 > lofs + lsamples)
        {
          // right side was cut off
          lz[2] = ofs - lofs;
        }
        else
        {
          // split into 2 parts
          lz[2] = ofs - lofs;

          let zoomer = new Array(3);
          zoomer[0] = ofs + samples;
          zoomer[1] = lz[1];
          zoomer[2] = lofs + lsamples - zoomer[0];
          lastZoomers.splice(i + 1, 0, zoomer);
        }
      }

      i--;
      break;
    }
  }
  return lastZoomers;
}

function setCommonSvgData(svg, t, key, width, color, textContent)
{
  let title = svg.getElementsByTagName('title')[0];
  let rects = svg.getElementsByTagName('rect');
  let rectBg = rects[0];
  let rect = rects[1];
  let text = svg.getElementsByTagName('text')[0];

  title.textContent = t;

  svg.style['display'] = 'block';
  svg.setAttribute('heobKey', key);
  svg.setAttribute('width', width);

  rectBg.setAttribute('width', width);

  rect.setAttribute('fill', color);
  rect.setAttribute('width', width);

  text.textContent = textContent;
}

function zoomArr(zoomers)
{
  lastZoomers = zoomers;

  let zoomSamples = 0;
  let minStack = suppressStacks === 0 ? 0 : maxStack;
  for (let i = 0; i < zoomers.length; i++)
  {
    zoomSamples += zoomers[i][2];
    minStack = Math.min(minStack, zoomers[i][1]);
  }
  if (suppressStacks > 0) minStack = suppressStacks;

  resetCountMap(addrCountMap);
  resetCountMap(sourceCountMap);
  resetCountMap(funcCountMap);

  threadMap.forEach(
    function (value, key, map)
    {
      value[0] = 0;
      value[1] = 0;
      value[3] = 0;
    });

  let svgs = document.getElementsByTagName('svg');
  let zidx = 0;
  let pos = 0;
  let cidx = 0;
  let cpos = -1;
  let cend = 0;
  let cfinish = 0;
  if (zoomers.length > 0)
  {
    cpos = zoomers[0][0];
    cend = cpos + zoomers[0][2];
    cfinish = zoomers[zoomers.length - 1][0] + zoomers[zoomers.length - 1][2];
  }
  let maxExtention = 0;
  let sumBytes = 0;
  let sumAllocs = 0;
  let blockedCount = 0;
  let stackShowMin = minStack > 3 ? minStack - 1 : 0;
  let stackShowDiff = stackShowMin ? stackShowMin - 2 : 0;
  let visibleStack = 0;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] === undefined) continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);
    let allocs = 0;
    let svgType = svg.attributes['heobAllocs'] === undefined;
    if (!svgType)
      allocs = parseInt(svg.attributes['heobAllocs'].value);

    if (zidx < zoomers.length &&
        ofs + 0.1 >= zoomers[zidx][0] + zoomers[zidx][2])
    {
      pos += zoomers[zidx][2];
      zidx++;
    }
    if (zidx >= zoomers.length || ofs + samples - 0.1 <= zoomers[zidx][0])
    {
      svg.style['display'] = 'none';
      continue;
    }

    if (stack > 0 && stack < stackShowMin)
    {
      svg.style['display'] = 'none';
      continue;
    }

    if (stack > visibleStack)
      visibleStack = stack;

    if (svg.attributes['heobBlocked'] !== undefined)
      blockedCount++;

    let x1 = pos;
    if (ofs > zoomers[zidx][0])
      x1 += ofs - zoomers[zidx][0];
    let x2 = pos;
    for (let j = zidx; j < zoomers.length && ofs + samples > zoomers[j][0]; j++)
    {
      if (ofs + samples > zoomers[j][0] + zoomers[j][2])
        x2 += zoomers[j][2];
      else
        x2 += ofs + samples - zoomers[j][0];
    }
    let shownSamples = x2 - x1;

    if (mapType === svgType)
    {
      updateCountMap(addrCountMap, svg, 'heobAddr',
          ofs, samples, shownSamples, allocs);
      updateCountMap(sourceCountMap, svg, 'heobSource',
          ofs, samples, shownSamples, allocs);
      updateCountMap(funcCountMap, svg, 'heobFunc',
          ofs, samples, shownSamples, allocs);

      let thread;
      if (svg.attributes['heobThread'] !== undefined)
        thread = svg.attributes['heobThread'].value;
      let mapEntry;
      if (thread !== undefined)
        mapEntry = threadMap.get(thread);
      if (mapEntry !== undefined && ofs + 0.1 >= mapEntry[1])
      {
        mapEntry[0] += shownSamples;
        mapEntry[1] = ofs + samples;
        mapEntry[3] += allocs;
      }
    }

    let x = spacer + x1 * fullWidth / zoomSamples;
    let y = headerHeight + (maxStack - stack - 1) * 16;
    let width = shownSamples * fullWidth / zoomSamples;
    let height = 16;
    let opacity = stack >= zoomers[zidx][1] ? 1 : 0.5;

    if (stack > 1)
      y += stackShowDiff * 16;

    svg.setAttribute('x', x);
    svg.setAttribute('y', y);
    svg.setAttribute('width', width);
    svg.setAttribute('height', height);
    svg.style['display'] = 'block';
    svg.style['opacity'] = opacity;
    svg.setAttribute('heobOpacity', opacity);

    let rects = svg.getElementsByTagName('rect');
    for (let j = 0; j < 2; j++)
      rects[j].setAttribute('width', width);

    if (ofs + 0.1 > maxExtention && stack > 0)
    {
      if (allocs > 0.1)
        sumBytes += shownSamples;
      maxExtention = ofs + samples;
    }

    if (stack > 0 && ofs + samples - 0.1 < cend &&
        ((allocs < 0.1 && ofs - 0.1 < cpos && ofs + samples - 0.1 > cpos) ||
         (allocs > 0.1 && Math.abs(ofs - cpos) < 0.1)))
    {
      if (allocs > 0.1)
        sumAllocs += allocs;
      cpos = ofs + samples;

      if (cpos + 0.1 > cend && cidx + 1 < zoomers.length)
      {
        cidx++;
        cpos = zoomers[cidx][0];
        cend = cpos + zoomers[cidx][2];
      }
    }
  }
  let blockedSvg = document.getElementById('blocked');
  setButtonVisible(blockedSvg, blockedCount);

  for (let i = 4; i < maxStack; i++)
  {
    let minusSvg = document.getElementById('minus' + i);
    if (i < stackShowMin || i > visibleStack || i === suppressStacks)
    {
      minusSvg.style['display'] = 'none';
      continue;
    }
    let plusMinusY = headerHeight +
      (maxStack - i - 1 + stackShowDiff) * 16 + 3.5;
    minusSvg.setAttribute('y', plusMinusY);
    minusSvg.style['display'] = 'block';
  }

  sortCountMap(addrCountMap, addrCountArr);
  sortCountMap(sourceCountMap, sourceCountArr);
  sortCountMap(funcCountMap, funcCountArr);

  let showArrs = [addrCountArr, sourceCountArr, funcCountArr];
  let showMaps = [addrCountMap, sourceCountMap, funcCountMap];
  let showCount = 0;
  for (let i = 0; i < 3; i++)
  {
    let arr = showArrs[i];
    let l = arr.length;
    if (l === 0) continue;
    let map0 = showMaps[i].get(arr[0]);
    if (map0[2] === 0 || map0[0] < 2) l = 0;
    if (l > showCount) showCount = l;
  }

  threadSort();
  let threadCount = threadArray.length;
  if (threadCount > 1 && threadMap.get(threadArray[1])[0] === 0)
    threadCount = 0;

  halfWidth = fullWidth;
  if (showCount > 0 && threadCount > 0)
    halfWidth = (fullWidth - spacer) / 2;

  for (let i = 0; i < 3; i++)
  {
    let svg = document.getElementById('showType' + i);
    setButtonVisible(svg, showCount);
  }

  showTypeData();

  let maxThreadSamples = 0;
  for (let i = 0; i < threadArray.length; i++)
  {
    let key = threadArray[i];
    let value = threadMap.get(key);

    let svg = document.getElementById('thread' + i);

    let sum = value[0];
    if (sum === 0 || i >= threadCount)
    {
      svg.style['display'] = 'none';
      continue;
    }

    if (maxThreadSamples === 0)
      maxThreadSamples = sum;

    let refSvg = value[2];

    let width = Math.max(sum * halfWidth / maxThreadSamples, 2);
    let x = spacer + fullWidth - width;
    let color = value[4];
    if (color === undefined)
      color = refSvg.attributes['heobColor'].value;

    let t = withNL(key) +
      sumText(mapType ? sum : 0, mapType ? 0 : sum, value[3]);

    setCommonSvgData(svg, t, key, width, color, key);

    svg.setAttribute('x', x);
  }

  if (cpos + 0.1 < cfinish)
    sumAllocs = 0;
  infoTextReset = sumText(zoomSamples - sumBytes, sumBytes, sumAllocs);
}

function showTypeData(maxShowSamples)
{
  for (let i = 0; i < maxCount; i++)
  {
    let svg = document.getElementById('addr' + i);
    if (svg === undefined) break;

    if (i >= showArr.length)
    {
      svg.style['display'] = 'none';
      continue;
    }

    let key = showArr[i];
    let value = showMap.get(key);

    let sum = value[2];
    if (sum === 0 || value[0] < 2)
    {
      svg.style['display'] = 'none';
      continue;
    }

    if (maxShowSamples === undefined)
      maxShowSamples = sum;

    let refSvg = value[4];

    let width = Math.max(sum * halfWidth / maxShowSamples, 2);
    if (width > halfWidth) width = halfWidth;
    let color = refSvg.attributes['heobColor'].value;

    let textContent = refSvg.getElementsByTagName('text')[0].textContent;
    if (searchRe !== undefined && textContent.search(searchRe) >= 0)
      color = '#ffffff';

    let t = withNL(funcAttribute(refSvg));
    if (value[5]) t += withNL(sourceAttribute(refSvg, true));
    if (value[6]) t += withNL(addrModAttribute(refSvg, true));
    t += sumText(mapType ? sum : 0, mapType ? 0 : sum, value[4]);

    setCommonSvgData(svg, t, key, width, color, textContent);
  }
}

function setButtonEnabled(svg, b)
{
  if (!b)
  {
    svg.setAttribute('class', '');
    svg.style['opacity'] = 0.5;
  }
  else
  {
    svg.setAttribute('class', 'sample');
    svg.style['opacity'] = 1;
  }
}

function setButtonVisible(svg, b)
{
  if (!b)
    svg.style['display'] = 'none';
  else
    svg.style['display'] = 'block';
}

function showType(t, refresh)
{
  if (t === 0)
  {
    showMap = funcCountMap;
    showArr = funcCountArr;
    showKey = 'heobFunc';
  }
  else if (t === 1)
  {
    showMap = sourceCountMap;
    showArr = sourceCountArr;
    showKey = 'heobSource';
  }
  else
  {
    showMap = addrCountMap;
    showArr = addrCountArr;
    showKey = 'heobAddr';
  }

  for (let i = 0; i < 3; i++)
  {
    let svg = document.getElementById('showType' + i);
    setButtonEnabled(svg, i !== t);
  }

  if (refresh)
    showTypeData();
}

function zoom(e, svg)
{
  if (e.button !== 0) return;

  functionTextReset = funcAttribute(svg);
  sourceTextReset = sourceAttribute(svg);
  addressTextReset = addrModAttribute(svg);
  threadTextReset = threadAttribute(svg);

  zoomArr(getZoomers(svg));
}

function delZoom(e, svg)
{
  if (e.button !== 1) return;

  let zoomers = getZoomers(svg);
  zoomers = zoomersAndNot(zoomers, e.ctrlKey);
  zoomArr(zoomers);
  infoClear();
}

function addrZoom(e, svg)
{
  if (e.button !== 0) return;

  let key = svg.attributes['heobKey'].value;

  if (e.ctrlKey)
  {
    let value = showMap.get(key);
    let sum = value[2];
    showTypeData(sum);
    return;
  }

  let zoomersRet = getAddrZoomers(key);
  let zoomers = zoomersRet[0];
  let foundSvg = zoomersRet[1];

  zoomArr(zoomers);

  functionTextReset = funcAttribute(foundSvg);
  sourceTextReset = sourceAttribute(foundSvg);
  addressTextReset = addrModAttribute(foundSvg);
  threadTextReset = '';
  infoClear();
}

function addrDelZoom(e, svg)
{
  if (e.button !== 1) return;

  let key = svg.attributes['heobKey'].value;
  let zoomersRet = getAddrZoomers(key);
  let zoomers = zoomersRet[0];
  let foundSvg = zoomersRet[1];

  zoomers = zoomersAndNot(zoomers, e.ctrlKey);
  zoomArr(zoomers);
  infoClear();
}

function threadZoom(e, svg)
{
  if (e.button !== 0) return;

  let key = svg.attributes['heobKey'].value;
  let zoomers = getThreadZoomers(key);
  zoomArr(zoomers);

  functionTextReset = key;
  sourceTextReset = '';
  addressTextReset = '';
  threadTextReset = '';
  infoClear();
}

function threadDelZoom(e, svg)
{
  if (e.button !== 1) return;

  let key = svg.attributes['heobKey'].value;
  let zoomers = getThreadZoomers(key);
  zoomers = zoomersAndNot(zoomers, e.ctrlKey);
  zoomArr(zoomers);
  infoClear();
}

function blockedZoom(e)
{
  if (e.button !== 0) return;

  let zoomers = getBlockedZoomers();
  zoomArr(zoomers);

  functionTextReset = '';
  sourceTextReset = '';
  addressTextReset = '';
  threadTextReset = '';
  infoClear();
}

function blockedDelZoom(e)
{
  if (e.button !== 1) return;

  let zoomers = getBlockedZoomers();
  zoomers = zoomersAndNot(zoomers, e.ctrlKey);
  zoomArr(zoomers);
  infoClear();
}
