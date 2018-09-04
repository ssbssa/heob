const ADDR_SUM_COUNT = 100;

var headerHeight = 50;
var footerHeight = 90;
var spacer = 10;

var maxStack = 0;
var sampleTimes = 0;
var fullWidth = 0;
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

function heobInit()
{
  let svgs = document.getElementsByTagName('svg');
  let svgNs = svgs[0].getAttribute('xmlns');

  let rect0 = addRectPara(svgNs, '100%', '100%', '#cccccc');
  svgs[0].insertBefore(rect0, svgs[0].firstChild);

  svgs[0].onmousedown = function(e) { if (e.button == 1) return false; }

  maxStack = -1;
  let bottom, zoomSvg;
  let addrMap = new Map();
  let threadMap = new Map();
  let firstSvg;
  let mapType;
  let minStack = 0;
  let sumSamples = 0;
  let sumAllocs = 0;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] == undefined) continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);

    if (firstSvg == undefined && stack > 0)
      firstSvg = svg;

    if (stack == 1)
    {
      if (svg.attributes['heobAllocs'] == undefined)
        sumSamples += samples;
      else
        sumAllocs += parseInt(svg.attributes['heobAllocs'].value);
    }

    if (stack == 1 && zoomSvg == undefined)
      zoomSvg = svg;
    else if (stack == 0)
    {
      bottom = svg;
      if (samples > parseInt(zoomSvg.attributes['heobSum'].value) + 0.1)
        zoomSvg = svg;
      else
        minStack = 1;

      sampleTimes = parseInt(svg.attributes['heobSamples'].value);

      svg.setAttribute('heobText',
          sumText(sumSamples, samples - sumSamples, sumAllocs));
    }

    if (stack > maxStack) maxStack = stack;

    let color;
    let threadColor;
    let arrayInMap;
    let addrMapKey = getAddrMapKey(svg);
    if (addrMapKey != undefined)
    {
      arrayInMap = addrMap.get(addrMapKey);
      if (arrayInMap == undefined)
      {
        arrayInMap = new Array(5);
        arrayInMap[0] = 0;   // sum of samples
        arrayInMap[1] = 0;   // number of different occurences of address
        arrayInMap[2] = 0;   // maximum extention
        arrayInMap[3] = svg; // data reference
        arrayInMap[4] = 0;   // sum of allocation count
        addrMap.set(addrMapKey, arrayInMap);
      }
      else
        color = arrayInMap[3].attributes['heobColor'].value;
    }
    if (color == undefined)
    {
      if (stack <= 1)
        color = createBaseColor();
      else if (svg.attributes['heobSource'] != undefined)
        color = createSourceColor();
      else if (svg.attributes['heobFunc'] != undefined)
        color = createFuncColor();
      else if (svg.attributes['heobAddr'] != undefined)
        color = createAddrColor();
      else
      {
        if (svg.attributes['heobThread'] != undefined)
        {
          let mapEntry = threadMap.get(svg.attributes['heobThread'].value);
          if (mapEntry != undefined)
            color = mapEntry[4];
        }
        if (color == undefined)
        {
          color = createThreadColor();
          threadColor = color;
        }
      }
    }

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
    if (svg.attributes['heobFunc'] != undefined)
      t = svg.attributes['heobFunc'].value;
    else if (svg.attributes['heobSource'] != undefined)
      t = svg.attributes['heobSource'].value;
    else if (svg.attributes['heobMod'] != undefined)
    {
      t = svg.attributes['heobMod'].value;
      let delim = t.lastIndexOf('\\');
      if (delim >= 0)
        t = t.substr(delim + 1);
    }
    else if (svg.attributes['heobAddr'] != undefined)
      t = svg.attributes['heobAddr'].value;
    else if (svg.attributes['heobThread'] != undefined)
      t = svg.attributes['heobThread'].value;
    else
      continue;
    addText(svg, svgNs, t);

    if (arrayInMap != undefined && stack > 1 &&
        // only the first entry of an address in a stacktrace is used
        ofs + 0.1 >= arrayInMap[2])
    {
      let svgType = svg.attributes['heobAllocs'] == undefined;
      if (mapType == undefined)
        mapType = svgType;

      if (mapType == svgType)
      {
        arrayInMap[0] += samples;
        arrayInMap[1]++;
        arrayInMap[2] = ofs + samples;
        if (!svgType)
          arrayInMap[4] += parseInt(svg.attributes['heobAllocs'].value);
      }
    }

    if (svg.attributes['heobThread'] != undefined)
    {
      let svgType = svg.attributes['heobAllocs'] == undefined;
      if (mapType == undefined)
        mapType = svgType;

      let thread = svg.attributes['heobThread'].value;
      let mapEntry = threadMap.get(thread);
      if (mapEntry == undefined)
      {
        mapEntry = new Array(5);
        mapEntry[0] = 0;           // sum of samples
        mapEntry[1] = 0;           // maximum extention
        mapEntry[2] = svg;         // data reference
        mapEntry[3] = 0;           // sum of allocation count
        mapEntry[4] = threadColor; // color
        threadMap.set(thread, mapEntry);
      }
      else if (threadColor != undefined)
        mapEntry[4] = threadColor;
      else if (mapEntry[4] == undefined && ofs + 0.1 >= mapEntry[1])
        mapEntry[4] = createThreadColor();
      if (ofs + 0.1 >= mapEntry[1] && mapType == svgType)
      {
        mapEntry[0] += samples;
        mapEntry[1] = ofs + samples;
        if (!svgType)
          mapEntry[3] += parseInt(svg.attributes['heobAllocs'].value);
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
      if (svg.attributes['heobStack'] == undefined) continue;

      svg.setAttribute('heobStack',
          parseInt(svg.attributes['heobStack'].value) - 1);
    }
    maxStack--;
  }
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] == undefined) continue;

    let t = withNL(funcAttribute(svg)) + withNL(sourceAttribute(svg)) +
      withNL(addrModAttribute(svg)) + withNL(sumAttribute(svg)) +
      withNL(threadAttribute(svg));
    svg.getElementsByTagName('title')[0].textContent = t;
  }

  let svg = svgs[0];

  if (firstSvg != undefined && bottom != undefined)
    svg.insertBefore(bottom, firstSvg);

  let svgWidth = parseInt(svg.width.baseVal.value);
  fullWidth = svgWidth - 2 * spacer;
  let fullHeight = headerHeight + maxStack * 16 + footerHeight;

  let addrSumArray = new Array();
  addrMap.forEach(
    function (value, key, map)
    {
      if (value[0] > 0 && value[1] >= 2)
        addrSumArray.push(key);
    });
  addrSumArray.sort(
    function (a, b)
    {
      return addrMap.get(b)[0] - addrMap.get(a)[0];
    });

  let threadArray = new Array();
  threadMap.forEach(
    function (value, key, map)
    {
      if (value[0] > 0)
        threadArray.push(key);
    });
  threadArray.sort(
    function (a, b)
    {
      let sumSamplesA = threadMap.get(a)[0];
      let sumSamplesB = threadMap.get(b)[0];
      if (sumSamplesA != sumSamplesB)
        return sumSamplesB - sumSamplesA;
      return a.localeCompare(b, undefined, {numeric: true});
    });
  if (threadArray.length == 1)
    threadArray.pop();

  let extraCount = Math.max(Math.min(addrSumArray.length, ADDR_SUM_COUNT),
      threadArray.length);
  let svgHeight = fullHeight;
  if (extraCount > 0)
    svgHeight += 40 + extraCount * 16;
  svg.setAttribute('height', svgHeight);
  svg.setAttribute('viewBox', '0 0 ' + svgWidth + ' ' + svgHeight);

  let halfWidth = fullWidth;
  if (addrSumArray.length > 0 && threadArray.length > 0)
    halfWidth = (fullWidth - spacer) / 2;

  let addrCount = 0;
  let maxAddrSamples = 0;
  for (let i = 0; i < addrSumArray.length; i++)
  {
    if (addrCount >= ADDR_SUM_COUNT) break;

    let key = addrSumArray[i];
    let value = addrMap.get(key);

    let sum = value[0];
    let refSvg = value[3];

    if (maxAddrSamples == 0)
      maxAddrSamples = sum;

    let x = spacer;
    let y = fullHeight + 31 + addrCount * 16;
    let width = Math.max(sum * halfWidth / maxAddrSamples, 2);
    let height = 16;
    let color = refSvg.attributes['heobColor'].value;

    let newSvg = createSvg(svgNs, x, y, width, height);

    let t = withNL(funcAttribute(refSvg)) + withNL(sourceAttribute(refSvg)) +
      withNL(addrModAttribute(refSvg)) +
      sumText(mapType ? sum : 0, mapType ? 0 : sum, value[4]);
    addTitle(newSvg, svgNs, t);

    addRectBg(newSvg, svgNs, rect0);

    addRect(newSvg, svgNs, color);

    addText(newSvg, svgNs, refSvg.getElementsByTagName('text')[0].textContent);

    newSvg.setAttribute('heobKey', key);
    newSvg.setAttribute('onmouseover', 'addrInfoSet(this)');
    newSvg.setAttribute('onmouseout', 'addrInfoClear()');
    newSvg.setAttribute('onclick', 'addrZoom(evt, this)');
    newSvg.setAttribute('onmousedown', 'delAddrZoom(evt, this)');

    svg.appendChild(newSvg);

    addrCount++;
  }

  let maxThreadSamples = 0;
  for (let i = 0; i < threadArray.length; i++)
  {
    let key = threadArray[i];
    let value = threadMap.get(key);

    let sum = value[0];
    let refSvg = value[2];
    let color = value[4];
    if (color == undefined)
      color = refSvg.attributes['heobColor'].value;

    if (maxThreadSamples == 0)
      maxThreadSamples = sum;

    let width = Math.max(sum * halfWidth / maxThreadSamples, 2);
    let height = 16;
    let x = spacer + fullWidth - width;
    let y = fullHeight + 31 + i * 16;

    let newSvg = createSvg(svgNs, x, y, width, height);

    let t = withNL(key) +
      sumText(mapType ? sum : 0, mapType ? 0 : sum, value[3]);
    addTitle(newSvg, svgNs, t);

    addRectBg(newSvg, svgNs, rect0);

    addRect(newSvg, svgNs, color);

    addText(newSvg, svgNs, key);

    newSvg.setAttribute('heobKey', key);
    newSvg.setAttribute('onmouseover', 'threadInfoSet(this)');
    newSvg.setAttribute('onmouseout', 'addrInfoClear()');
    newSvg.setAttribute('onclick', 'threadZoom(evt, this)');
    newSvg.setAttribute('onmousedown', 'delThreadZoom(evt, this)');

    svg.appendChild(newSvg);
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

  if (zoomSvg != undefined)
  {
    // fake left mouse button
    let e = { button: 0 };
    zoom(e, zoomSvg);
  }
  infoClear();
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

function createSvg(svgNs, x, y, width, height)
{
  let newSvg = document.createElementNS(svgNs, 'svg');
  newSvg.setAttribute('class', 'sample');
  newSvg.setAttribute('x', x);
  newSvg.setAttribute('y', y);
  newSvg.setAttribute('width', width);
  newSvg.setAttribute('height', height);
  newSvg.style['display'] = 'block';
  return newSvg;
}

function addTitle(par, svgNs, t)
{
  let newTitle = document.createElementNS(svgNs, 'title');
  newTitle.textContent = t;
  par.appendChild(newTitle);
}

function addText(par, svgNs, t)
{
  let newText = document.createElementNS(svgNs, 'text');
  newText.setAttribute('x', '2');
  newText.setAttribute('y', '10.5');
  newText.setAttribute('font-size', '12');
  newText.setAttribute('font-family', 'Verdana');
  newText.textContent = t;
  par.appendChild(newText);
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

function attributeToText(svg, attr)
{
  if (svg.attributes[attr] != undefined)
    return svg.attributes[attr].value;
  else
    return '';
}

function funcAttribute(svg)
{
  return attributeToText(svg, 'heobFunc');
}

function sourceAttribute(svg)
{
  return attributeToText(svg, 'heobSource');
}

function addrModAttribute(svg)
{
  let tAddr = attributeToText(svg, 'heobAddr');
  let tMod = attributeToText(svg, 'heobMod');
  if (tAddr.length == 0 && tMod.length > 0)
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
    t += samples + ' samples (' +
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
  if (svg.attributes['heobText'] != undefined)
    return svg.attributes['heobText'].value;

  let sum = parseInt(svg.attributes['heobSum'].value);
  if (svg.attributes['heobAllocs'] != undefined)
    return sumText(0, sum, parseInt(svg.attributes['heobAllocs'].value));
  else
    return sumText(sum, 0, 0);
}

function threadAttribute(svg)
{
  return attributeToText(svg, 'heobThread');
}

function withNL(t)
{
  if (t.length == 0)
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

function getAddrMapKey(svg)
{
  let mapKey;
  if (svg.attributes['heobSource'] != undefined)
    mapKey = svg.attributes['heobSource'].value;
  else if (svg.attributes['heobAddr'] != undefined)
    mapKey = svg.attributes['heobAddr'].value;
  return mapKey;
}

function addrInfoSet(svg)
{
  let key = svg.attributes['heobKey'].value;
  let svgs = document.getElementsByTagName('svg');
  for (let i = 0; i < svgs.length; i++)
  {
    let svgEntry = svgs[i];
    if (svgEntry.attributes['heobSum'] == undefined) continue;

    let opacity = parseFloat(svgEntry.attributes['heobOpacity'].value);
    let entryKey = getAddrMapKey(svgEntry);
    if (entryKey == undefined || entryKey != key)
      opacity *= 0.25;
    else
      opacity = 1;
    svgEntry.style['opacity'] = opacity;
  }
}

function addrInfoClear()
{
  let svgs = document.getElementsByTagName('svg');
  for (let i = 0; i < svgs.length; i++)
  {
    let svgEntry = svgs[i];
    if (svgEntry.attributes['heobSum'] == undefined) continue;

    let opacity = parseFloat(svgEntry.attributes['heobOpacity'].value);
    svgEntry.style['opacity'] = opacity;
  }
}

function threadInfoSet(svg)
{
  let key = svg.attributes['heobKey'].value;
  let svgs = document.getElementsByTagName('svg');
  for (let i = 0; i < svgs.length; i++)
  {
    let svgEntry = svgs[i];
    if (svgEntry.attributes['heobSum'] == undefined) continue;

    let opacity = parseFloat(svgEntry.attributes['heobOpacity'].value);
    if (svgEntry.attributes['heobThread'] == undefined ||
        svgEntry.attributes['heobThread'].value != key)
      opacity *= 0.25;
    else
      opacity = 1;
    svgEntry.style['opacity'] = opacity;
  }
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

function getAddrZoomers(key)
{
  let svgs = document.getElementsByTagName('svg');
  let zoomers = new Array();
  let maxExtention = 0;
  let foundSvg;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] == undefined) continue;

    let entryKey = getAddrMapKey(svg);
    if (entryKey == undefined || entryKey != key)
      continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    if (ofs + 0.1 < maxExtention)
      continue;

    if (foundSvg == undefined)
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

function getThreadZoomers(key)
{
  let svgs = document.getElementsByTagName('svg');
  let zoomers = new Array();
  let maxExtention = 0;
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] == undefined) continue;

    if (svg.attributes['heobThread'] == undefined ||
        svg.attributes['heobThread'].value != key)
      continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    if (ofs + 0.1 < maxExtention)
      continue;

    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);

    maxExtention = ofs + samples;

    let zoomer = new Array(3);
    zoomer[0] = ofs;
    zoomer[1] = stack;
    zoomer[2] = samples;
    zoomers.push(zoomer);
  }
  return zoomers;
}

function zoomersAndNot(zoomers)
{
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

function zoomArr(zoomers)
{
  lastZoomers = zoomers;

  let zoomSamples = 0;
  for (let i = 0; i < zoomers.length; i++)
    zoomSamples += zoomers[i][2];

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
  for (let i = 0; i < svgs.length; i++)
  {
    let svg = svgs[i];
    if (svg.attributes['heobSum'] == undefined) continue;

    let ofs = parseInt(svg.attributes['heobOfs'].value);
    let stack = parseInt(svg.attributes['heobStack'].value);
    let samples = parseInt(svg.attributes['heobSum'].value);
    let allocs = 0;
    if (svg.attributes['heobAllocs'] != undefined)
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

    let x = spacer + x1 * fullWidth / zoomSamples;
    let y = headerHeight + (maxStack - stack - 1) * 16;
    let width = (x2 - x1) * fullWidth / zoomSamples;
    let height = 16;
    let opacity = stack >= zoomers[zidx][1] ? 1 : 0.5;

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
        sumBytes += x2 - x1;
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

  if (cpos + 0.1 < cfinish)
    sumAllocs = 0;
  infoTextReset = sumText(zoomSamples - sumBytes, sumBytes, sumAllocs);
}

function zoom(e, svg)
{
  if (e.button != 0) return;

  functionTextReset = funcAttribute(svg);
  sourceTextReset = sourceAttribute(svg);
  addressTextReset = addrModAttribute(svg);
  threadTextReset = threadAttribute(svg);

  zoomArr(getZoomers(svg));
}

function delZoom(e, svg)
{
  if (e.button != 1) return;

  let zoomers = getZoomers(svg);
  zoomers = zoomersAndNot(zoomers);
  zoomArr(zoomers);
  infoClear();
}

function addrZoom(e, svg)
{
  if (e.button != 0) return;

  let key = svg.attributes['heobKey'].value;
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

function delAddrZoom(e, svg)
{
  if (e.button != 1) return;

  let key = svg.attributes['heobKey'].value;
  let zoomersRet = getAddrZoomers(key);
  let zoomers = zoomersRet[0];
  let foundSvg = zoomersRet[1];

  zoomers = zoomersAndNot(zoomers);
  zoomArr(zoomers);
  infoClear();
}

function threadZoom(e, svg)
{
  if (e.button != 0) return;

  let key = svg.attributes['heobKey'].value;
  let zoomers = getThreadZoomers(key);
  zoomArr(zoomers);

  functionTextReset = key;
  sourceTextReset = '';
  addressTextReset = '';
  threadTextReset = '';
  infoClear();
}

function delThreadZoom(e, svg)
{
  if (e.button != 1) return;

  let key = svg.attributes['heobKey'].value;
  let zoomers = getThreadZoomers(key);
  zoomers = zoomersAndNot(zoomers);
  zoomArr(zoomers);
  infoClear();
}
