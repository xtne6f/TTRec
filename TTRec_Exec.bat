@echo off
echo 【予約開始・終了時バッチの参考例】
echo ・タイミングはExecOnStartRec=予約開始直後、ExecOnEndRec=予約終了の2秒後
echo ・バックグラウンドで起動するのでバッチ処理中に次の予約が始まることもある
echo ・「連続する予約イベントを1ファイルにまとめる」のときは最後の予約の終了後に
echo   起動する。このときイベント名などは最初の予約のものが格納される
echo ・カレントディレクトリはTVTest.exeのある場所
echo ・各種情報は環境変数に格納される
echo ・バッチでなくてもいい
echo   (例)ExecOnEndRec=""C:\windows\system32\wscript" "Plugins\TTRec_Exec.vbs""
echo.
echo 実行タイミング       %TTRecExec%
echo 予約開始日時         %TTRecStartTime%
echo 予約の長さ           %TTRecDuration%
echo OriginalNetworkID    %TTRecONID%
echo TransportStreamID    %TTRecTSID%
echo ServiceID            %TTRecSID%
echo EventID              %TTRecEID%
echo.
echo ※以下は予約開始時と終了時のカウント数の差から求めたもの
echo ※予約開始から10秒以内のカウントは反映されない
echo ※計測失敗(エラーカウントリセットしたときなど)のとき-1になる
echo ドロップ数           %TTRecDrops%
echo エラー数             %TTRecErrors%
echo スクランブル数       %TTRecScrambles%
echo.
echo ※以下は「見るだけ」のときセットされない
echo ※コマンドライン引数にも格納される(バッチではこっちのほうが便利)
echo ファイルのフルパス   "%TTRecFilePath%"
echo コマンドライン引数   "%~1"
echo.
echo ※以下はEPG情報そのものなのでファイル名に使用できない記号を含むかもしれない
echo サービス名           "%TTRecServiceName%"
echo イベント開始日時     %TTRecEventStartTime%
echo イベントの長さ       %TTRecEventDuration%
echo イベント名           "%TTRecEventName%"
setlocal EnableDelayedExpansion
echo (イベントテキスト)
echo "!TTRecEventText!"
echo (イベント拡張テキスト)
echo "!TTRecEventExText!"
endlocal
echo.
echo 【ファイル名にエラーカウントを付ける例】
echo if "%TTRecExec%"=="EndRec" if exist "%~1" rename "%~1" "%~n1-D%TTRecDrops%E%TTRecErrors%S%TTRecScrambles%%~x1"
pause
