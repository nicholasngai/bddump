# bddump: The Open-Source MakeMKV CLI Alternative

bddump is a very simple project: I wanted to use open-source software and a command-line interface in order to back up copies of my Blu-ray discs off of my Blu-ray player, without going through non-free alternatives such as [MakeMKV](https://makemkv.com/).

## Dependencies

bddump is built on top of your favorite open-source libraries. The following dependencies are required to build this project:

- [libavformat](https://ffmpeg.org/libavformat.html)
- [libbluray](https://www.videolan.org/developers/libbluray.html)

## Building

```sh
make
```

## Usage

### List Titles

List titles on a Blu-ray disc:

```sh
./bddump -d <device> -l
```

| Parameter | Required | Description                                                                       |
| --------- | -------- | --------------------------------------------------------------------------------- |
| `-d`      | Yes      | The device containing the Blu-ray disc. Usually something like /dev/sr0 on Linux. |
| `-l`      | Yes      | List titles                                                                       |

### Read Video Data

Extract video data off of the Blu-ray disc and output to an MKV file:

```sh
./bddump -d <device> [-a <AACS KEYDB.cfg path>] -t <title> [-x <excluded stream index>] -o <out file>
```

| Parameter | Required | Description                                                                                                                                                                                   |
| --------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `-d`      | Yes      | The device containing the Blu-ray disc. Usually something like /dev/sr0 on Linux.                                                                                                             |
| `-t`      | Yes      | The index of the title to read, obtained from the `-l` parameter                                                                                                                              |
| `-o`      | Yes      | The output file path, in MKV format. `-` may be specified as the path to output to stdout.                                                                                                    |
| `-a`      | No       | The path to a KEYDB.cfg file containing AACS keys, if your disc is AACS-encrypted. This may be obtained from <https://fvonline-db.bplaced.net/>.                                              |
| `-x`      | No       | The index of a stream within the title to exclude. bddump outputs each stream's codec information when running which may be used to identify problematic streams. This is usually not needed. |

The `-o -` option to output to stdout may be used in combination with ffmpeg if you wish to re-encode or remux the video format before writing to disc. For example, to remux to an MP4 container, you can use:

```sh
./bddump -d <device> -t <title> -o - | ffmpeg -i - -map v -map a -c copy output.mp4
```

(Note that since MP4 files can't contain bitmap subtitles, subtitle data will be lost with this method.)
