/*
 * Copyright (C) 2017-2018 Alibaba Group Holding Limited. All Rights Reserved.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//jscs:disable
/* jshint ignore:start */
"use strict";
process.env.CAFUI2 = true;
var Page = require("yunos/page/Page");
var View = require("yunos/ui/view/View");
var TextView = require("yunos/ui/view/TextView");
var SurfaceView = require("yunos/ui/view/SurfaceView");
var native = nativeLoad("sample.so");

class SamplePage extends Page {
    onStart() {
        var title = new TextView();
        title.height = 300;
        title.width = this.window.width;
        title.fontSize = 40;
        title.color = "white";
        title.background = "#FF6600"
        title.verticalAlign = TextView.VerticalAlign.Middle;
        title.text = "YunOS NDK Graphics Demo";
        title.align = TextView.Align.Center;
        this.window.addChild(title);

        var surfaceView = this._surfaceView = new SurfaceView();
        surfaceView.top = title.bottom;
        surfaceView.width = this.window.width;
        surfaceView.height = this.window.height - title.height - title.top;
        surfaceView.surfaceType = SurfaceView.SurfaceType.Local;
        surfaceView.on("ready", () => {
            surfaceView.setZOrderOnTop(true);
            let surfaceID = surfaceView.getSurfaceId();
            console.log("surfaceId: " + surfaceID);
            native.startRender("", surfaceID, surfaceView.width, surfaceView.height);
            native.redraw(surfaceView.width/2, surfaceView.height/2);

        });

        surfaceView.on("touchmove", (e) => {
            console.warn("touch move x: " + e.touches[0].clientX + " y: " + e.touches[0].clientY);
            native.redraw(e.touches[0].clientX, e.touches[0].clientY);
            console.warn("redraw end")
        });

        this.window.addChild(surfaceView);
    }

    onStop() {
        console.log("page stop");
        native.stopRender();
    }
}
module.exports = SamplePage;
/* jshint ignore:end */
