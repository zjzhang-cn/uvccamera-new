this project is base on @saki(t_saki@serenegiant.com)'s UVCCamera project https://github.com/saki4510t/UVCCamera with the following changes:

- Delete some less used module to keep the entire project simple;
- Migrate to android X;
- Optimize some libusb's call, improve compatibility;
- Change the UVCPreview thread from asynchronous to synchronous;
- Solve uvccamera stopPreview crash or blocking issue;
- Remove the requirement of surface when fetching YUV;
- Use libYUV to convert mjpeg to YUV to improve efficiency.


UVCCamera是基于安卓的一个USB外接摄像头调用项目,该项目允许安卓设备在不需要root的情况下打开外接USB摄像头进行预览,获取视频帧.

最近在基于UVCCamera适配一款设备的时候出现了问题:该设备始终无法打开一款摄像头(项目必须要适配这款摄像头),作为对比,我的安卓手机就没有任何问题.作为一个负责任的开发人员,我只好硬着头皮肝下去.并最终解决了这个问题.

经过研究,发现UVCCamera项目确实有这些缺点:

- 兼容性不是太好,经常出现不同的手机,或者不同摄像头上面表现不一致的问题;
- 停止预览(stopPreview)时,偶尔会阻死,接口无法返回;
- native层线程较多,比较杂乱.感觉并不需要这么多线程呀,这也是上一条阻死的原因;
- 视频采集和显示具有强耦合,发现如果不设置surface,则不会启动预览线程,从而无法采集;
- 视频格式转换时效率较低;

我在UVCCamera基础上,修改了上面的问题.修改方法如下:

- 删除了一些module,保留最简单一个demo.
- 适配AndroidX.
- 优化了libusb调用方法,提高了兼容性,在那个原先有故障的设备上测试通过,故障排除;
- 解决了stopPreview阻死的问题;
- native层使用轮询模式(之前是回调模式),一方面减少了线程数,一方面可以根据业务逻辑动态调节帧率;
- 去掉采集对surface的依赖;
- 使用libyuv进行格式转换,提高了效率.

更多说明见中文博客 https://editor.csdn.net/md/?articleId=121084301
也可加入QQ群交流:879258392


library and sample to access to UVC web camera on non-rooted Android device

Copyright (c) 2014-2017 saki t_saki@serenegiant.com

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

All files in the folder are under this Apache License, Version 2.0. Files in the jni/libjpeg, jni/libusb and jin/libuvc folders may have a different license, see the respective files.
