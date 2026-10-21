import { FlutterAbility, FlutterEngine, MethodChannel, MethodCall, MethodResult } from '@ohos/flutter_ohos';
import { GeneratedPluginRegistrant } from '../plugins/GeneratedPluginRegistrant';
import picker from '@ohos.file.picker';
import fs from '@ohos.file.fs';

// Installed into the SDK-generated host by scripts/build_ohos.sh.
export default class EntryAbility extends FlutterAbility {
  private channel?: MethodChannel;
  private busy: boolean = false;

  configureFlutterEngine(engine: FlutterEngine): void {
    super.configureFlutterEngine(engine);
    GeneratedPluginRegistrant.registerWith(engine);
    this.channel = new MethodChannel(engine.getDartExecutor().getBinaryMessenger(), 'local.photo/files');
    this.channel.setMethodCallHandler({
      onMethodCall: (call: MethodCall, result: MethodResult): void => {
        if (call.method === 'supportDirectory') {
          result.success(this.context.filesDir);
          return;
        }
        if (call.method !== 'open' && call.method !== 'save') {
          result.notImplemented();
          return;
        }
        if (this.busy) {
          result.error('busy', '已有文件选择窗口打开', null);
          return;
        }
        this.busy = true;
        this.documents(call, result);
      }
    });
  }

  private async documents(call: MethodCall, result: MethodResult): Promise<void> {
    const documents = new picker.DocumentViewPicker(this.context);
    const saving = call.method === 'save';
    try {
      if (saving) {
        const options = new picker.DocumentSaveOptions();
        options.newFileNames = [call.argument<string>('name')!];
        const selected = await documents.save(options);
        if (selected.length === 0) { result.success(false); return; }
        const output = await fs.open(selected[0], fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
        try { await fs.copyFile(call.argument<string>('path')!, output.fd); }
        finally { await fs.close(output); }
        result.success(true);
      } else {
        const options = new picker.DocumentSelectOptions();
        options.maxSelectNumber = call.argument<boolean>('multiple') ? 12 : 1;
        const selected = await documents.select(options);
        const paths: string[] = [];
        const root = `${this.context.filesDir}/imports`;
        if (!fs.accessSync(root)) { fs.mkdirSync(root); }
        for (let index = 0; index < selected.length; ++index) {
          const source = await fs.open(selected[index], fs.OpenMode.READ_ONLY);
          const target = `${root}/${Date.now()}-${index}.input`;
          try {
            const stat = await fs.stat(source.fd);
            if (stat.size > 256 * 1024 * 1024) { throw new Error('文件超过移动端 256MB 上限'); }
            await fs.copyFile(source.fd, target);
          } finally { await fs.close(source); }
          paths.push(target);
        }
        result.success(paths);
      }
    } catch (error) {
      result.error('file', `${error}`, null);
    } finally { this.busy = false; }
  }
}
