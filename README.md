# Single-word Arbitration Multiple Accessのシミュレーター

## 概要

多数のデバイスが一つの高速・長距離の有線バスに繋がっていた時に、時刻同期を行って無駄なく高速に通信を行える手法をシミュレーションするためのコード。

C++2aのCoroutine TSを使っている。

## 依存関係
* Clang 7.0以上
* CMake 3.05以上
* libc++

全てaptで入る。

## ビルドの方法

    mkdir build-release
    cd build-release
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make
    
## 実行方法

    # CSMA/CDのシミュレーション
    ./csma_cd_sim 0.2 0.1 0.1    # コマンドラインオプションにメッセージの生成確率(平均生成メッセージ数÷パケットサイズ)を人数分指定する
    
    
    # Single-word Arbitrationのシミュレーション
    ./new_method 0.2 0.1 0.1    # コマンドラインオプションにメッセージの生成確率(平均生成メッセージ数÷パケットサイズ)を人数分指定する
    

