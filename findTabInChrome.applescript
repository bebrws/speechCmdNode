-- https://apple.stackexchange.com/questions/273970/applescript-to-find-a-tab-by-its-name-in-google-chrome


set searchString to text returned of (display dialog "Enter a string to search for:" default answer "" with title "Find Google Chrome Tab")

tell application "Google Chrome"
    set win_List to every window
    set win_MatchList to {}
    set tab_MatchList to {}
    set tab_NameMatchList to {}
    repeat with win in win_List
        set tab_list to every tab of win
        repeat with t in tab_list
            if searchString is in (title of t as string) then
                set end of win_MatchList to win
                set end of tab_MatchList to t
                set end of tab_NameMatchList to (id of win as string) & ".  " & (title of t as string)
            end if
        end repeat
    end repeat
    if (count of tab_MatchList) is equal to 1 then
        set w to item 1 of win_MatchList
        set index of w to 1
        my setActiveTabIndex(t, searchString)
    else if (count of tab_MatchList) is equal to 0 then
        display dialog "No match was found!" buttons {"OK"} default button 1
    else
        set which_Tab to choose from list of tab_NameMatchList with prompt "The following Tabs matched, please select one:"
        if which_Tab is not equal to false then
            set oldDelims to (get AppleScript's text item delimiters)
            set AppleScript's text item delimiters to "."
            set tmp to text items of (which_Tab as string)
            set w to (item 1 of tmp) as integer
            set AppleScript's text item delimiters to oldDelims
            set index of window id w to 1
            my setActiveTabIndex(t, searchString)
        end if
    end if
end tell

on setActiveTabIndex(t, searchString)
    tell application "Google Chrome"
        set i to 0
        repeat with t in tabs of front window
            set i to i + 1
            if title of t contains searchString then
                set active tab index of front window to i
                return
            end if
        end repeat
    end tell
end setActiveTabIndex


